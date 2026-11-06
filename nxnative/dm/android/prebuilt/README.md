# Prebuilt Host Binaries (libdmhost.so)

This directory holds verified release builds of `libdmhost.so` for games tested on this fork:

- `0100A00019DE0000/libdmhost.so` (Hades II v655360):
  Built with working-set texture eviction, zero-copy lazy pool realization, and page-dirty tracking fixes for Xiaomi Poco F1 (Snapdragon 845, LineageOS 22.2 Android 15).
- `010040801A4C8000/libdmhost.so` (Blasphemous 2 v524288):
  Built and tested on Xiaomi Poco F1.

These prebuilt libraries can be used by the port pipeline when building on systems without the Android NDK installed.
