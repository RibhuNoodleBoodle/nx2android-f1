# The Long Dark — Build and Installation Guide

## 1. Prerequisites

### Host Environment (Windows PC)
- **Python:** 3.10+ (with `capstone`, `zstd`, `lz4`, `pillow`, `pycryptodome`)
- **Android NDK:** r27c (NDK 27.2.12479018)
- **.NET SDK:** 10.0+ (for Ryujinx shader compiler)
- **ADB Platform Tools:** 35.0+ installed and in PATH
- **Keys:** Valid Switch `prod.keys` configured in `I:/Downloads/nx2android/prod.keys`

### Target Device (Connected Hardware)
- **Device:** Xiaomi Poco F1 (`beryllium`, Snapdragon 845 / Adreno 630)
- **OS:** LineageOS 22.2 (Android 15)
- **USB Debugging:** Authorized via ADB (`adb devices -l`)
- **MicroSD Card:** Mounted at `/storage/EA2B-B2FD` with >= 20 GB free space
- **Graphics Driver:** Mesa Turnip bundled via `AdrenoTools` (`vulkan.turnip.so`)

---

## 2. Port and Build Commands

### Step A: Port Game Files
Execute the porter pipeline with target package, Mesa Turnip driver, 40 FPS frame cap, and physical memory constraints:
```powershell
python -m porter port `
  --vulkan-driver "turnip_mrpurple_T30-toasted.adpkg.zip" `
  --device 8c92f94d `
  --package org.nx2a.t01007a700a87c000 `
  "I:\Downloads\The-Long-Dark-Switch-NSP-Base-Game"
```

### Step B: RomFS Placement on Removable MicroSD
Bulk RomFS data (~16 GB) is pushed to the application-specific external microSD directory:
```powershell
python -m porter install `
  --device 8c92f94d `
  --romfs-at "/storage/EA2B-B2FD/Android/data/org.nx2a.t01007a700a87c000/files/romfs" `
  --romfs-push `
  "I:\Downloads\The-Long-Dark-Switch-NSP-Base-Game\TheLongDark.apk"
```

### Step C: Launch and Logcat Monitoring
```powershell
adb -s 8c92f94d logcat -c
adb -s 8c92f94d shell am start -n org.nx2a.t01007a700a87c000/android.app.NativeActivity
adb -s 8c92f94d logcat -s NX2A:*
```

---

## 3. Runtime Verification Checklist

- [ ] Standalone offline execution (0 network permissions in manifest)
- [ ] Turnip driver active and verified in logcat (`[vk] loaded Vulkan driver: ...`)
- [ ] Hardware video decoding operational for intro/cutscenes (`intro_cards.mp4`, `main_titles.mp4`)
- [ ] Presentation paced at 40 FPS (25 ms interval)
- [ ] Physical memory within budget (target <= 3.2 GiB PSS, ceiling 4.0 GiB)
- [ ] RomFS loaded cleanly from removable microSD
