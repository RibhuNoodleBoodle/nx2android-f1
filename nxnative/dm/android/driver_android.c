// Android port: build-time bundled custom Vulkan driver loader (Option A).
// When porter packages a custom Adreno / Turnip driver into the APK (mkapk.py --extra-so → lib/arm64-v8a/libcustom_vulkan.so),
// Android extracts it alongside libdmhost.so in the app's nativeLibraryDir (/data/app/.../lib/arm64/).
// Because Android's Zygote / RenderThread / GraphicsEnv preloads /system/lib64/libvulkan.so and runs its EnsureInitialized()
// std::once_flag before vk_init() is reached, we load a fresh, private instance of /system/lib64/libvulkan.so via memfd_create
// with a patched DT_SONAME ("000vulkan.so") into a shared linker namespace, and hook android_dlopen_ext, android_load_sphal_library,
// dlopen, and hw_get_module across its ELF GOT/PLT (DT_JMPREL, DT_RELA, and DT_ANDROID_RELA) to redirect the Vulkan HAL load
// to <nativeLibraryDir>/libcustom_vulkan.so.
#define _GNU_SOURCE
#include "driver_android.h"
#include <android/dlext.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef LOG
#define LOG(...) do { fprintf(stderr, "[dm] " __VA_ARGS__); fputc('\n', stderr); } while (0)
#endif

#ifndef ANDROID_NAMESPACE_TYPE_SHARED
#define ANDROID_NAMESPACE_TYPE_SHARED 2ULL
#endif

#ifndef DT_ANDROID_RELA
#define DT_ANDROID_RELA   0x6000000f
#define DT_ANDROID_RELASZ 0x60000010
#endif

#ifndef __NR_memfd_create
#define __NR_memfd_create 279
#endif

struct hw_module_t;

typedef void *(*pfn_loader_dlopen)(const char *filename, int flags, const void *caller_addr);
typedef void *(*pfn_android_dlopen_ext)(const char *filename, int flags, const android_dlextinfo *extinfo);
typedef void *(*pfn_android_load_sphal_library)(const char *filename, int flags);
typedef void *(*pfn_dlopen)(const char *filename, int flags);
typedef int (*pfn_hw_get_module)(const char *id, const struct hw_module_t **module);

typedef struct android_namespace_t *(*pfn_create_namespace)(
    const char *name, const char *ld_library_path, const char *default_library_path,
    uint64_t type, const char *permitted_when_isolated_path,
    struct android_namespace_t *parent, const void *caller_addr);
typedef struct android_namespace_t *(*pfn_get_exported_namespace)(const char *name);
typedef bool (*pfn_link_namespaces)(
    struct android_namespace_t *ns_from, struct android_namespace_t *ns_to, const char *shared_libs_sonames);
typedef bool (*pfn_link_namespaces_all_libs)(
    struct android_namespace_t *ns_from, struct android_namespace_t *ns_to);

static pfn_android_dlopen_ext g_real_android_dlopen_ext;
static pfn_android_load_sphal_library g_real_android_load_sphal_library;
static pfn_dlopen g_real_dlopen;
static pfn_hw_get_module g_real_hw_get_module;

static pfn_create_namespace g_create_namespace;
static pfn_get_exported_namespace g_get_exported_namespace;
static pfn_link_namespaces g_link_namespaces;
static pfn_link_namespaces_all_libs g_link_namespaces_all_libs;
static struct android_namespace_t *g_default_ns;

static char g_native_lib_dir[512];
static char g_custom_driver_path[560];
static char g_lock_path[560];
static int g_custom_armed;
static int g_custom_loaded;
static unsigned g_unique_id;

static int env_on(const char *name) {
    const char *s = getenv(name);
    return s && *s && strcmp(s, "0") && strcasecmp(s, "false") && strcasecmp(s, "no");
}

static int find_loader_syms_cb(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size; (void)data;
    if (!info->dlpi_name) return 0;
    if (!strstr(info->dlpi_name, "libdl") && !strstr(info->dlpi_name, "ld-android") && !strstr(info->dlpi_name, "linker"))
        return 0;
    const Elf64_Dyn *dyn = NULL;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dyn = (const Elf64_Dyn *)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn) return 0;
    const Elf64_Sym *symtab = NULL;
    const char *strtab = NULL;
    const uint32_t *buckets = NULL;
    size_t nsyms = 0;
    for (const Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        uintptr_t p = d->d_un.d_ptr > info->dlpi_addr ? d->d_un.d_ptr : info->dlpi_addr + d->d_un.d_ptr;
        if (d->d_tag == DT_SYMTAB) symtab = (const Elf64_Sym *)p;
        else if (d->d_tag == DT_STRTAB) strtab = (const char *)p;
        else if (d->d_tag == DT_HASH) { buckets = (const uint32_t *)p; nsyms = buckets[1]; }
    }
    if (!symtab || !strtab) return 0;
    if (!nsyms && (uintptr_t)strtab > (uintptr_t)symtab)
        nsyms = ((uintptr_t)strtab - (uintptr_t)symtab) / sizeof(Elf64_Sym);
    if (!nsyms || nsyms > 4096) nsyms = 512;
    for (size_t i = 0; i < nsyms; i++) {
        if (!symtab[i].st_value || !symtab[i].st_name) continue;
        const char *name = strtab + symtab[i].st_name;
        void *addr = (void *)(info->dlpi_addr + symtab[i].st_value);
        if (!strcmp(name, "__loader_android_create_namespace") && !g_create_namespace)
            g_create_namespace = (pfn_create_namespace)addr;
        else if (!strcmp(name, "__loader_android_get_exported_namespace") && !g_get_exported_namespace)
            g_get_exported_namespace = (pfn_get_exported_namespace)addr;
        else if (!strcmp(name, "__loader_android_link_namespaces") && !g_link_namespaces)
            g_link_namespaces = (pfn_link_namespaces)addr;
        else if (!strcmp(name, "__loader_android_link_namespaces_all_libs") && !g_link_namespaces_all_libs)
            g_link_namespaces_all_libs = (pfn_link_namespaces_all_libs)addr;
    }
    return (g_create_namespace && g_link_namespaces_all_libs && g_get_exported_namespace) ? 1 : 0;
}

static void resolve_bionic_namespace_apis(void) {
    if (g_create_namespace && g_link_namespaces_all_libs) return;
    size_t pagesz = (size_t)sysconf(_SC_PAGESIZE);

    // Tier A (liblinkernsbypass method): walk dlopen's ARM64 instructions to locate __loader_dlopen,
    // then open ld-android.so and libdl_android.so using &dlopen (in the system namespace) as caller_addr.
    uintptr_t dlopen_addr = (uintptr_t)&dlopen;
    mprotect((void *)(dlopen_addr & ~(pagesz - 1)), pagesz, PROT_READ | PROT_WRITE | PROT_EXEC);
    const uint32_t *insn = (const uint32_t *)dlopen_addr;
    pfn_loader_dlopen loader_dlopen = NULL;
    for (int i = 0; i < 16; i++) {
        uint32_t op = insn[i] >> 26;
        if (op == 0x25 || op == 0x05) { // BL or B
            int32_t off = ((int32_t)(insn[i] << 6)) >> 6;
            loader_dlopen = (pfn_loader_dlopen)(insn + i + off);
            break;
        }
    }
    if (loader_dlopen) {
        mprotect((void *)((uintptr_t)loader_dlopen & ~(pagesz - 1)), pagesz, PROT_READ | PROT_WRITE | PROT_EXEC);
        void *ld_h = loader_dlopen("ld-android.so", RTLD_LAZY, (const void *)&dlopen);
        if (ld_h) {
            g_create_namespace = (pfn_create_namespace)dlsym(ld_h, "__loader_android_create_namespace");
            g_get_exported_namespace = (pfn_get_exported_namespace)dlsym(ld_h, "__loader_android_get_exported_namespace");
            g_link_namespaces = (pfn_link_namespaces)dlsym(ld_h, "__loader_android_link_namespaces");
            g_link_namespaces_all_libs = (pfn_link_namespaces_all_libs)dlsym(ld_h, "__loader_android_link_namespaces_all_libs");
        }
        void *dla_h = loader_dlopen("libdl_android.so", RTLD_LAZY, (const void *)&dlopen);
        if (dla_h) {
            if (!g_create_namespace) g_create_namespace = (pfn_create_namespace)dlsym(dla_h, "__loader_android_create_namespace");
            if (!g_get_exported_namespace) g_get_exported_namespace = (pfn_get_exported_namespace)dlsym(dla_h, "__loader_android_get_exported_namespace");
            if (!g_link_namespaces) g_link_namespaces = (pfn_link_namespaces)dlsym(dla_h, "__loader_android_link_namespaces");
            if (!g_link_namespaces_all_libs) g_link_namespaces_all_libs = (pfn_link_namespaces_all_libs)dlsym(dla_h, "__loader_android_link_namespaces_all_libs");
        }
    }

    // Tier B: dlsym(RTLD_DEFAULT) and dl_iterate_phdr fallback
    if (!g_create_namespace) g_create_namespace = (pfn_create_namespace)dlsym(RTLD_DEFAULT, "__loader_android_create_namespace");
    if (!g_get_exported_namespace) g_get_exported_namespace = (pfn_get_exported_namespace)dlsym(RTLD_DEFAULT, "__loader_android_get_exported_namespace");
    if (!g_link_namespaces) g_link_namespaces = (pfn_link_namespaces)dlsym(RTLD_DEFAULT, "__loader_android_link_namespaces");
    if (!g_link_namespaces_all_libs) g_link_namespaces_all_libs = (pfn_link_namespaces_all_libs)dlsym(RTLD_DEFAULT, "__loader_android_link_namespaces_all_libs");
    if (!g_create_namespace || !g_link_namespaces_all_libs || !g_get_exported_namespace)
        dl_iterate_phdr(find_loader_syms_cb, NULL);

    // Create a shared copy of the unrestricted default system namespace (passing &dlopen as caller_addr)
    if (g_create_namespace && !g_default_ns) {
        g_default_ns = g_create_namespace(
            "nx2a_default_copy", NULL, "/system/lib64",
            ANDROID_NAMESPACE_TYPE_SHARED, NULL, NULL, (const void *)&dlopen);
    }
}

static int is_hal_vulkan_path(const char *filename) {
    return filename && (strstr(filename, "vulkan.") || strstr(filename, "/hw/vulkan"));
}

static void *open_custom_icd(const char *orig_filename, int flags, const android_dlextinfo *extinfo) {
    void *h = NULL;
    // Tier 1: Create a shared linker namespace bridged to default_ns (/system/lib64: libhardware.so, libnativewindow.so,
    // libsync.so, liblog.so, libz.so) + sphal_ns (/vendor/lib64: libgsl.so, libc++.so) + nativeLibraryDir (libcustom_vulkan.so)
    if (g_create_namespace && g_real_android_dlopen_ext) {
        struct android_namespace_t *sphal_ns = (extinfo && (extinfo->flags & ANDROID_DLEXT_USE_NAMESPACE))
            ? extinfo->library_namespace : NULL;
        if (!sphal_ns && g_get_exported_namespace) {
            static const char *const ns_names[] = {"sphal", "vendor", "default"};
            for (int i = 0; i < 3 && !sphal_ns; i++)
                sphal_ns = g_get_exported_namespace(ns_names[i]);
        }
        char search_paths[768];
        snprintf(search_paths, sizeof search_paths, "%s:/vendor/lib64:/odm/lib64:/system/lib64", g_native_lib_dir);
        struct android_namespace_t *drv_ns = g_create_namespace(
            orig_filename ? orig_filename : "nx2a_custom_vulkan",
            g_native_lib_dir,
            search_paths,
            ANDROID_NAMESPACE_TYPE_SHARED,
            "/data:/vendor:/odm:/system:/apex",
            sphal_ns ? sphal_ns : g_default_ns,
            (const void *)&dlopen);
        if (drv_ns) {
            if (g_default_ns && g_link_namespaces_all_libs)
                g_link_namespaces_all_libs(drv_ns, g_default_ns);
            if (sphal_ns && g_link_namespaces_all_libs)
                g_link_namespaces_all_libs(drv_ns, sphal_ns);
            if (g_link_namespaces)
                g_link_namespaces(drv_ns, NULL, "libandroid.so");
            android_dlextinfo drv_ext = {0};
            drv_ext.flags = ANDROID_DLEXT_USE_NAMESPACE;
            drv_ext.library_namespace = drv_ns;
            h = g_real_android_dlopen_ext("libcustom_vulkan.so", flags | RTLD_NOW | RTLD_LOCAL, &drv_ext);
            if (!h)
                h = g_real_android_dlopen_ext(g_custom_driver_path, flags | RTLD_NOW | RTLD_LOCAL, &drv_ext);
            if (!h)
                LOG("[vk] namespace dlopen(%s) note: %s (trying standard dlopen)", g_custom_driver_path, dlerror());
        }
    }
    // Tier 2: Standard dlopen in the app's classloader-namespace
    if (!h) {
        h = (g_real_dlopen ? g_real_dlopen : dlopen)(g_custom_driver_path, RTLD_NOW | RTLD_LOCAL);
    }
    if (!h) {
        LOG("[RED] [vk] custom driver dlopen(%s) failed (%s): falling back to system driver (%s)",
            g_custom_driver_path, dlerror(), orig_filename ? orig_filename : "vulkan");
        return NULL;
    }
    // Tier 3: Validate the Android HAL module symbol (HMI)
    if (!dlsym(h, "HMI")) {
        LOG("[RED] [vk] custom driver %s has no HMI symbol: falling back to system driver (%s)",
            g_custom_driver_path, orig_filename ? orig_filename : "vulkan");
        dlclose(h);
        return NULL;
    }
    g_custom_loaded = 1;
    LOG("[vk] custom driver loaded: %s (redirected from %s)", g_custom_driver_path, orig_filename ? orig_filename : "vulkan");
    return h;
}

static void *hook_android_dlopen_ext(const char *filename, int flags, const android_dlextinfo *extinfo) {
    if (g_custom_armed && is_hal_vulkan_path(filename)) {
        void *h = open_custom_icd(filename, flags, extinfo);
        if (h) return h;
    }
    return g_real_android_dlopen_ext(filename, flags, extinfo);
}

static void *hook_android_load_sphal_library(const char *filename, int flags) {
    if (g_custom_armed && is_hal_vulkan_path(filename)) {
        void *h = open_custom_icd(filename, flags, NULL);
        if (h) return h;
    }
    if (g_real_android_load_sphal_library)
        return g_real_android_load_sphal_library(filename, flags);
    if (g_get_exported_namespace && g_real_android_dlopen_ext) {
        static const char *const ns_names[] = {"sphal", "vendor", "default"};
        for (int i = 0; i < 3; i++) {
            struct android_namespace_t *ns = g_get_exported_namespace(ns_names[i]);
            if (ns) {
                android_dlextinfo ext = {0};
                ext.flags = ANDROID_DLEXT_USE_NAMESPACE;
                ext.library_namespace = ns;
                return g_real_android_dlopen_ext(filename, flags, &ext);
            }
        }
    }
    return (g_real_dlopen ? g_real_dlopen : dlopen)(filename, flags);
}

static void *hook_dlopen(const char *filename, int flags) {
    if (g_custom_armed && is_hal_vulkan_path(filename)) {
        void *h = open_custom_icd(filename, flags, NULL);
        if (h) return h;
    }
    return (g_real_dlopen ? g_real_dlopen : dlopen)(filename, flags);
}

static int hook_hw_get_module(const char *id, const struct hw_module_t **module) {
    if (g_custom_armed && id && !strcmp(id, "vulkan") && module) {
        void *h = open_custom_icd("vulkan.hal.so", RTLD_NOW | RTLD_LOCAL, NULL);
        if (h) {
            const struct hw_module_t *hmi = (const struct hw_module_t *)dlsym(h, "HMI");
            if (hmi) {
                *module = hmi;
                return 0;
            }
        }
    }
    return g_real_hw_get_module ? g_real_hw_get_module(id, module) : -ENOENT;
}

static int patch_single_reloc(uintptr_t bias, Elf64_Addr r_offset, Elf64_Xword r_info,
                              const Elf64_Sym *symtab, const char *strtab, size_t pagesz) {
    uint32_t sym_idx = ELF64_R_SYM(r_info);
    uint32_t r_type = ELF64_R_TYPE(r_info);
    if (!sym_idx) return 0;
    if (r_type != R_AARCH64_JUMP_SLOT && r_type != R_AARCH64_GLOB_DAT && r_type != R_AARCH64_ABS64)
        return 0;
    const char *name = strtab + symtab[sym_idx].st_name;
    void *replacement = NULL;
    if (!strcmp(name, "android_dlopen_ext") && g_real_android_dlopen_ext)
        replacement = (void *)hook_android_dlopen_ext;
    else if (!strcmp(name, "android_load_sphal_library"))
        replacement = (void *)hook_android_load_sphal_library;
    else if (!strcmp(name, "dlopen") && g_real_dlopen)
        replacement = (void *)hook_dlopen;
    else if (!strcmp(name, "hw_get_module"))
        replacement = (void *)hook_hw_get_module;
    if (!replacement) return 0;
    void **slot = (void **)(bias + r_offset);
    uintptr_t page = (uintptr_t)slot & ~(pagesz - 1);
    if (mprotect((void *)page, pagesz, PROT_READ | PROT_WRITE) == 0) {
        *slot = replacement;
        return 1;
    }
    return 0;
}

static int patch_got_relas(uintptr_t bias, const Elf64_Rela *rela, size_t count,
                           const Elf64_Sym *symtab, const char *strtab, size_t pagesz) {
    int patched = 0;
    for (size_t i = 0; i < count; i++)
        patched += patch_single_reloc(bias, rela[i].r_offset, rela[i].r_info, symtab, strtab, pagesz);
    return patched;
}

static int64_t read_sleb128(const uint8_t **pp, const uint8_t *end) {
    int64_t result = 0;
    int shift = 0;
    uint8_t byte = 0;
    const uint8_t *p = *pp;
    while (p < end) {
        byte = *p++;
        result |= (int64_t)(byte & 0x7f) << shift;
        shift += 7;
        if (!(byte & 0x80)) break;
    }
    if (shift < 64 && (byte & 0x40))
        result |= -(((int64_t)1) << shift);
    *pp = p;
    return result;
}

static int patch_got_android_rela(uintptr_t bias, const uint8_t *aps2, size_t sz,
                                  const Elf64_Sym *symtab, const char *strtab, size_t pagesz) {
    if (!aps2 || sz < 4 || memcmp(aps2, "APS2", 4) != 0) return 0;
    const uint8_t *p = aps2 + 4, *end = aps2 + sz;
    size_t num_relocs = (size_t)read_sleb128(&p, end);
    Elf64_Addr r_offset = (Elf64_Addr)read_sleb128(&p, end);
    Elf64_Xword r_info = 0;
    Elf64_Sxword r_addend = 0;
    int patched = 0;
    for (size_t idx = 0; idx < num_relocs && p < end; ) {
        size_t group_size = (size_t)read_sleb128(&p, end);
        size_t group_flags = (size_t)read_sleb128(&p, end);
        size_t offset_delta = (group_flags & 2) ? (size_t)read_sleb128(&p, end) : 0;
        if (group_flags & 1) r_info = (Elf64_Xword)read_sleb128(&p, end);
        size_t addend_flags = group_flags & (8 | 4);
        if (addend_flags == (8 | 4)) r_addend += read_sleb128(&p, end);
        else if (addend_flags == 0) r_addend = 0;
        for (size_t i = 0; i < group_size && p < end; i++) {
            r_offset += (group_flags & 2) ? offset_delta : (Elf64_Addr)read_sleb128(&p, end);
            if (!(group_flags & 1)) r_info = (Elf64_Xword)read_sleb128(&p, end);
            if (addend_flags == 8) r_addend += read_sleb128(&p, end);
            (void)r_addend;
            patched += patch_single_reloc(bias, r_offset, r_info, symtab, strtab, pagesz);
        }
        idx += group_size;
    }
    return patched;
}

static int hook_elf_image(const Elf64_Ehdr *ehdr) {
    if (!ehdr || memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) return 0;
    const Elf64_Phdr *phdr = (const Elf64_Phdr *)((uintptr_t)ehdr + ehdr->e_phoff);
    uintptr_t bias = 0; int have_bias = 0;
    const Elf64_Dyn *dyn = NULL;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD && !have_bias) { bias = (uintptr_t)ehdr - phdr[i].p_vaddr; have_bias = 1; }
        else if (phdr[i].p_type == PT_DYNAMIC) dyn = (const Elf64_Dyn *)(bias + phdr[i].p_vaddr);
    }
    if (!dyn) return 0;
    const Elf64_Sym *symtab = NULL; const char *strtab = NULL;
    const Elf64_Rela *jmprel = NULL, *rela = NULL;
    const uint8_t *and_rela = NULL;
    size_t jmprel_sz = 0, rela_sz = 0, and_rela_sz = 0;
    for (const Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        uintptr_t p = d->d_un.d_ptr > bias ? d->d_un.d_ptr : bias + d->d_un.d_ptr;
        if (d->d_tag == DT_SYMTAB) symtab = (const Elf64_Sym *)p;
        else if (d->d_tag == DT_STRTAB) strtab = (const char *)p;
        else if (d->d_tag == DT_JMPREL) jmprel = (const Elf64_Rela *)p;
        else if (d->d_tag == DT_PLTRELSZ) jmprel_sz = d->d_un.d_val;
        else if (d->d_tag == DT_RELA) rela = (const Elf64_Rela *)p;
        else if (d->d_tag == DT_RELASZ) rela_sz = d->d_un.d_val;
        else if (d->d_tag == DT_ANDROID_RELA) and_rela = (const uint8_t *)p;
        else if (d->d_tag == DT_ANDROID_RELASZ) and_rela_sz = d->d_un.d_val;
    }
    if (!symtab || !strtab) return 0;
    size_t pagesz = (size_t)sysconf(_SC_PAGESIZE);
    int patched = 0;
    if (jmprel && jmprel_sz) patched += patch_got_relas(bias, jmprel, jmprel_sz / sizeof(Elf64_Rela), symtab, strtab, pagesz);
    if (rela && rela_sz) patched += patch_got_relas(bias, rela, rela_sz / sizeof(Elf64_Rela), symtab, strtab, pagesz);
    if (and_rela && and_rela_sz) patched += patch_got_android_rela(bias, and_rela, and_rela_sz, symtab, strtab, pagesz);
    return patched;
}

static int hook_loaded_libs_cb(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    int *total = (int *)data;
    if (!info->dlpi_name || !info->dlpi_addr) return 0;
    if (strstr(info->dlpi_name, "vulkan") || strstr(info->dlpi_name, "libvndksupport.so") || strstr(info->dlpi_name, "libhardware.so")) {
        for (int i = 0; i < info->dlpi_phnum; i++) {
            if (info->dlpi_phdr[i].p_type == PT_LOAD) {
                const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
                *total += hook_elf_image(ehdr);
                break;
            }
        }
    }
    return 0;
}

static int patch_elf_soname_in_mem(uint8_t *mapped, size_t sz, const char *prefix3) {
    if (sz < sizeof(Elf64_Ehdr) || memcmp(mapped, ELFMAG, SELFMAG) != 0) return 0;
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)mapped;
    // Method 1: Section headers (SHT_DYNAMIC)
    if (ehdr->e_shoff && ehdr->e_shnum && ehdr->e_shoff + (size_t)ehdr->e_shnum * sizeof(Elf64_Shdr) <= sz) {
        const Elf64_Shdr *shdrs = (const Elf64_Shdr *)(mapped + ehdr->e_shoff);
        for (uint16_t i = 0; i < ehdr->e_shnum; i++) {
            if (shdrs[i].sh_type == SHT_DYNAMIC && shdrs[i].sh_link < ehdr->e_shnum) {
                char *strtab = (char *)(mapped + shdrs[shdrs[i].sh_link].sh_offset);
                const Elf64_Dyn *dyn = (const Elf64_Dyn *)(mapped + shdrs[i].sh_offset);
                size_t count = shdrs[i].sh_size / sizeof(Elf64_Dyn);
                for (size_t k = 0; k < count && dyn[k].d_tag != DT_NULL; k++) {
                    if (dyn[k].d_tag == DT_SONAME) {
                        char *soname = strtab + dyn[k].d_un.d_val;
                        for (int c = 0; c < 3 && soname[c] && prefix3[c]; c++) soname[c] = prefix3[c];
                        return 1;
                    }
                }
            }
        }
    }
    // Method 2: Program headers (PT_DYNAMIC + PT_LOAD vaddr-to-offset)
    if (ehdr->e_phoff && ehdr->e_phnum && ehdr->e_phoff + (size_t)ehdr->e_phnum * sizeof(Elf64_Phdr) <= sz) {
        const Elf64_Phdr *phdrs = (const Elf64_Phdr *)(mapped + ehdr->e_phoff);
        const Elf64_Dyn *dyn = NULL;
        for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
            if (phdrs[i].p_type == PT_DYNAMIC && phdrs[i].p_offset + phdrs[i].p_filesz <= sz) {
                dyn = (const Elf64_Dyn *)(mapped + phdrs[i].p_offset);
                break;
            }
        }
        if (dyn) {
            uintptr_t strtab_vaddr = 0, soname_off = 0;
            int have_soname = 0;
            for (const Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
                if (d->d_tag == DT_STRTAB) strtab_vaddr = d->d_un.d_ptr;
                else if (d->d_tag == DT_SONAME) { soname_off = d->d_un.d_val; have_soname = 1; }
            }
            if (strtab_vaddr && have_soname) {
                for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
                    if (phdrs[i].p_type == PT_LOAD && phdrs[i].p_vaddr <= strtab_vaddr && strtab_vaddr < phdrs[i].p_vaddr + phdrs[i].p_filesz) {
                        size_t foff = phdrs[i].p_offset + (strtab_vaddr - phdrs[i].p_vaddr) + soname_off;
                        if (foff + 3 < sz) {
                            char *soname = (char *)(mapped + foff);
                            for (int c = 0; c < 3 && soname[c] && prefix3[c]; c++) soname[c] = prefix3[c];
                            return 1;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

static void *open_unique_libvulkan(const char *orig_vk_path, const char *tmp_dir) {
    if (!g_real_android_dlopen_ext) return NULL;
    const char *src_path = (orig_vk_path && orig_vk_path[0] == '/') ? orig_vk_path : "/system/lib64/libvulkan.so";
    int src_fd = open(src_path, O_RDONLY | O_CLOEXEC);
    if (src_fd < 0 && strcmp(src_path, "/system/lib64/libvulkan.so") != 0)
        src_fd = open("/system/lib64/libvulkan.so", O_RDONLY | O_CLOEXEC);
    if (src_fd < 0) return NULL;

    struct stat st;
    if (fstat(src_fd, &st) != 0 || st.st_size <= (off_t)sizeof(Elf64_Ehdr)) {
        close(src_fd);
        return NULL;
    }
    size_t sz = (size_t)st.st_size;

    int dst_fd = (int)syscall(__NR_memfd_create, "nx2a_libvulkan", 1 /* MFD_CLOEXEC */);
    if (dst_fd < 0 && tmp_dir && *tmp_dir) {
        char pbuf[560];
        snprintf(pbuf, sizeof pbuf, "%s/nx2a_%u_vulkan.so", tmp_dir, g_unique_id);
        dst_fd = open(pbuf, O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0600);
        unlink(pbuf);
    }
    if (dst_fd < 0) {
        close(src_fd);
        return NULL;
    }
    if (ftruncate(dst_fd, (off_t)sz) != 0) {
        close(src_fd);
        close(dst_fd);
        return NULL;
    }
    uint8_t *mapped = (uint8_t *)mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, dst_fd, 0);
    if (mapped == MAP_FAILED) {
        close(src_fd);
        close(dst_fd);
        return NULL;
    }
    size_t got = 0;
    while (got < sz) {
        ssize_t r = read(src_fd, mapped + got, sz - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(src_fd);
    if (got != sz) {
        munmap(mapped, sz);
        close(dst_fd);
        return NULL;
    }

    char prefix[4];
    snprintf(prefix, sizeof prefix, "%03u", (g_unique_id++) % 1000u);
    int patched_soname = patch_elf_soname_in_mem(mapped, sz, prefix);
    munmap(mapped, sz);
    if (!patched_soname) {
        close(dst_fd);
        return NULL;
    }

    struct android_namespace_t *vk_ns = NULL;
    if (g_create_namespace) {
        vk_ns = g_create_namespace(
            "nx2a_libvulkan", g_native_lib_dir, "/system/lib64",
            ANDROID_NAMESPACE_TYPE_SHARED, NULL, NULL, (const void *)&dlopen);
        if (vk_ns && g_default_ns && g_link_namespaces_all_libs)
            g_link_namespaces_all_libs(vk_ns, g_default_ns);
    }

    char fd_path[64];
    snprintf(fd_path, sizeof fd_path, "/proc/self/fd/%d", dst_fd);
    void *unique_lib = NULL;
    if (vk_ns) {
        android_dlextinfo ext = {0};
        ext.flags = ANDROID_DLEXT_USE_NAMESPACE | ANDROID_DLEXT_USE_LIBRARY_FD;
        ext.library_fd = dst_fd;
        ext.library_namespace = vk_ns;
        unique_lib = g_real_android_dlopen_ext(fd_path, RTLD_NOW | RTLD_LOCAL, &ext);
    }
    if (!unique_lib) {
        android_dlextinfo ext = {0};
        ext.flags = ANDROID_DLEXT_USE_LIBRARY_FD;
        ext.library_fd = dst_fd;
        unique_lib = g_real_android_dlopen_ext(fd_path, RTLD_NOW | RTLD_LOCAL, &ext);
    }
    close(dst_fd);
    return unique_lib;
}

void *dm_driver_open_vulkan(int force_system, void **out_gipa) {
    g_custom_armed = 0;
    g_custom_loaded = 0;
    void *sys_lib = (g_real_dlopen ? g_real_dlopen : dlopen)("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!sys_lib) return NULL;
    void *sys_gipa = dlsym(sys_lib, "vkGetInstanceProcAddr");
    if (out_gipa) *out_gipa = sys_gipa;
    if (!sys_gipa || force_system) return sys_lib;

    if (env_on("DM_VK_SYSTEM_DRIVER")) {
        LOG("[vk] DM_VK_SYSTEM_DRIVER is set: using system Vulkan driver");
        return sys_lib;
    }

    Dl_info self_dli;
    if (!dladdr((void *)dm_driver_open_vulkan, &self_dli) || !self_dli.dli_fname) return sys_lib;
    const char *slash = strrchr(self_dli.dli_fname, '/');
    if (!slash) return sys_lib;
    snprintf(g_native_lib_dir, sizeof g_native_lib_dir, "%.*s", (int)(slash - self_dli.dli_fname), self_dli.dli_fname);
    snprintf(g_custom_driver_path, sizeof g_custom_driver_path, "%s/libcustom_vulkan.so", g_native_lib_dir);
    if (access(g_custom_driver_path, R_OK) != 0) return sys_lib;

    // Crash-loop sentinel and writable Mesa shader cache / temp directory in DM_TEMP (<internalDataPath>/temp)
    const char *tmp_dir = getenv("DM_TEMP");
    if (tmp_dir && *tmp_dir) {
        setenv("TMPDIR", tmp_dir, 0);
        setenv("XDG_CACHE_HOME", tmp_dir, 0);
        setenv("MESA_SHADER_CACHE_DIR", tmp_dir, 0);
        snprintf(g_lock_path, sizeof g_lock_path, "%s/vk_driver_loading.lock", tmp_dir);
        if (access(g_lock_path, F_OK) == 0) {
            unlink(g_lock_path);
            LOG("[RED] [vk] bundled custom driver crashed during previous initialization; falling back to system driver");
            return sys_lib;
        }
        int fd = open(g_lock_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd >= 0) {
            (void)write(fd, "1\n", 2);
            fsync(fd);
            close(fd);
        }
    }

    g_real_android_dlopen_ext = (pfn_android_dlopen_ext)dlsym(RTLD_DEFAULT, "android_dlopen_ext");
    g_real_android_load_sphal_library = (pfn_android_load_sphal_library)dlsym(RTLD_DEFAULT, "android_load_sphal_library");
    g_real_dlopen = (pfn_dlopen)dlsym(RTLD_DEFAULT, "dlopen");
    g_real_hw_get_module = (pfn_hw_get_module)dlsym(RTLD_DEFAULT, "hw_get_module");
    resolve_bionic_namespace_apis();

    Dl_info sys_vk_dli = {0};
    dladdr(sys_gipa, &sys_vk_dli);

    // Load a fresh, uninitialized instance of libvulkan.so via memfd with patched DT_SONAME so that
    // EnsureInitialized()'s std::once_flag (which Zygote / RenderThread may have already triggered on the
    // default libvulkan.so) runs fresh and calls Hal::Open() -> LoadDriver() through our hooked GOT/PLT.
    void *active_lib = open_unique_libvulkan(sys_vk_dli.dli_fname, tmp_dir);
    int is_unique = (active_lib != NULL);
    if (!active_lib) active_lib = sys_lib;

    void *active_gipa = dlsym(active_lib, "vkGetInstanceProcAddr");
    if (!active_gipa) {
        active_lib = sys_lib;
        active_gipa = sys_gipa;
        is_unique = 0;
    }
    if (out_gipa) *out_gipa = active_gipa;

    g_custom_armed = 1;
    int patched = 0;
    Dl_info active_dli = {0};
    if (dladdr(active_gipa, &active_dli) && active_dli.dli_fbase)
        patched += hook_elf_image((const Elf64_Ehdr *)active_dli.dli_fbase);
    dl_iterate_phdr(hook_loaded_libs_cb, &patched);

    LOG("[vk] bundled custom driver found at %s: hook armed on %s libvulkan.so (patched=%d, ns_api=%d)",
        g_custom_driver_path, is_unique ? "fresh" : "system", patched, g_create_namespace != NULL);
    return active_lib;
}

void dm_driver_mark_ok(void) {
    if (g_lock_path[0]) unlink(g_lock_path);
}

void dm_driver_disable_custom(void) {
    g_custom_armed = 0;
    g_custom_loaded = 0;
    dm_driver_mark_ok();
}

int dm_driver_custom_armed(void) { return g_custom_armed; }
int dm_driver_custom_loaded(void) { return g_custom_loaded; }
