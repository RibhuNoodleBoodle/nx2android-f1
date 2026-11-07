// Android port: instruction and data cache maintenance
#pragma once
#include <stddef.h>
#include <stdint.h>

static inline void sys_icache_invalidate(void *start, size_t len) {
    __builtin___clear_cache((char *)start, (char *)start + len);
}

static inline void sys_dcache_flush(void *start, size_t len) {
#if defined(__aarch64__)
    if (!start || !len) return;
    uintptr_t addr = (uintptr_t)start;
    uintptr_t end = addr + len;
    uint64_t ctr = 0;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    size_t line = 4 << ((ctr >> 16) & 0xf);
    if (!line) line = 64;
    addr &= ~(line - 1);
    while (addr < end) {
        __asm__ volatile("dc civac, %0" : : "r"(addr) : "memory");
        addr += line;
    }
    __asm__ volatile("dsb sy\nisb" : : : "memory");
#else
    (void)start; (void)len;
#endif
}
