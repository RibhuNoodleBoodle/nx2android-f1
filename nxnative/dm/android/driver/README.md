# Bundled Custom Vulkan Driver (Adreno 630 / Snapdragon 845)

This folder contains the prebuilt Turnip Mesa Vulkan ICD driver for Qualcomm Adreno 6xx devices (tested extensively on Xiaomi Poco F1 / Snapdragon 845 running Android 15 LineageOS 22.2):

- **Binary**: `libcustom_vulkan.so` (18.8 MB ELF 64-bit ARM aarch64 shared library)
- **Metadata**: `meta.json`
  - Name: Turnip Adreno Driver T30 (`@Mr_Purple_666`)
  - Vendor: Mesa
  - Driver Version: 26.3.0-T30-1.4.359
  - Vulkan Version: 1.4.359 (supports timeline semaphores, dynamic rendering, imageless framebuffer, BCn texture decompression)

When porting a game (via the GUI **Port** button or CLI `porter port`), this custom driver is bundled into the APK under `lib/arm64-v8a/libcustom_vulkan.so` by default if no `--driver` override is specified. At runtime, `driver_android.c` redirects the hardware ICD load to `libcustom_vulkan.so`, enabling full Vulkan 1.3/1.4 features on older Adreno devices whose stock OEM driver lacks required extensions.
