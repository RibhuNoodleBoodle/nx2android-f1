// Android port: bundled custom Vulkan driver loader (Option A) with automatic fallback to system driver.
// Looks for <nativeLibraryDir>/libcustom_vulkan.so alongside libdmhost.so and hooks a fresh libvulkan.so's
// HAL loader (android_dlopen_ext / android_load_sphal_library / dlopen / hw_get_module) with a 3-tier fallback.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Opens libvulkan.so (a fresh memfd instance when a custom driver is armed, or system libvulkan.so on fallback)
// and resolves vkGetInstanceProcAddr into *out_gipa.
// If force_system is non-zero, bypasses any bundled custom driver and opens the stock system driver.
void *dm_driver_open_vulkan(int force_system, void **out_gipa);

// Clears the crash-loop sentinel lock file once vkCreateDevice succeeds.
void dm_driver_mark_ok(void);

// Disables the custom driver hook (used before retrying vk_init with the stock system driver).
void dm_driver_disable_custom(void);

// Returns 1 if a bundled custom driver hook is currently armed, 0 otherwise.
int dm_driver_custom_armed(void);

// Returns 1 if the bundled custom driver was actually loaded by libvulkan.so, 0 otherwise.
int dm_driver_custom_loaded(void);

#ifdef __cplusplus
}
#endif
