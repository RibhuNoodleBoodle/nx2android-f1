# On-Device Test & Verification Report: The Long Dark (Switch -> Poco F1)

## 1. Test Environment & Target Identity
- **Host System:** Windows 10/11 x64, Python 3.14.0, Android NDK r27c
- **Target Device:** Xiaomi Poco F1 (`beryllium`, serial: `8c92f94d`)
- **OS / Platform:** LineageOS 22.2 (Android 15, API Level 35)
- **SoC / CPU:** Qualcomm Snapdragon 845 (4x Kryo 385 Gold @ 2.8 GHz + 4x Kryo 385 Silver @ 1.77 GHz)
- **GPU & Driver:** Qualcomm Adreno 630 with bundled Mesa Turnip 26.3.0-T30 (`libvulkan.purple.so`)
- **Physical RAM:** 5.7 GB total, ~2.8 GB free at preflight
- **Storage Configuration:**
  - Internal storage: LineageOS root & app sandbox (`/sdcard/Android/data/org.nx2a.t01007a700a87c000/files`)
  - Removable microSD card: Volume `EA2B-B2FD` (`/storage/EA2B-B2FD/Android/data/org.nx2a.t01007a700a87c000/files/romfs`), 41 GB free

---

## 2. Software & Asset Provenance
- **Switch Title ID:** `01007A700A87C000` (The Long Dark)
- **Base Game Version:** `v0` (`cf59b702ea28d613eb8d2d864fbc61ec`)
- **Active Update:** `v983040` / Publisher Version `2.57.182420` (IL2CPP Unity engine on Switch SDK 20.5.6)
- **Installed DLC:** `01007A700A87D001` *Tales from the Far Territory* (`v262144`)
- **Total Compiled Shaders:** 39,171 SPIR-V shaders packaged into `shaders.dmsv` (199.5 MB)
- **APK Identity:**
  - Package Name: `org.nx2a.t01007a700a87c000`
  - APK Size: 22.0 MB (bundled with `libvulkan.purple.so`, `libdmhost.so`, `libadrenotools.so`)
  - Manifest Permissions: 0 network permissions declared (`INTERNET`, `ACCESS_NETWORK_STATE`, and `BLUETOOTH` completely absent)

---

## 3. Storage Layout & Removable MicroSD Verification
- **App Sandbox Path:** `/sdcard/Android/data/org.nx2a.t01007a700a87c000/files` (holds `env.txt`, `log.txt`, `shaders.dmsv`, `mods/`, and `aoc/`)
- **RomFS Bulk Storage:** `/storage/EA2B-B2FD/Android/data/org.nx2a.t01007a700a87c000/files/romfs` (3,043 files, 2.8 GB)
- **Environment Switches (`env.txt`):**
  ```ini
  DM_LANG=en-US
  DM_AOC_DIR=/sdcard/Android/data/org.nx2a.t01007a700a87c000/files/aoc
  DM_APP_POOL=3500M
  DM_FPS_PIN=40
  DM_SHARED_FONT_EXT=/sdcard/Android/data/org.nx2a.t01007a700a87c000/files/nintendo_ext.ttf
  DM_VK_DRIVER=libvulkan.purple.so
  DM_ROMFS=/storage/EA2B-B2FD/Android/data/org.nx2a.t01007a700a87c000/files/romfs
  ```

---

## 4. Hardware Verification & Constraints Checklist
| Requirement | Status | Metric / Evidence | Notes |
|---|---|---|---|
| Zero Networking Permissions | **PASS** | Manifest inspection confirms zero network/bluetooth permissions | Strictly offline single-player |
| Removable MicroSD Storage | **PASS** | RomFS deployed to `/storage/EA2B-B2FD/...` | App-specific hierarchy preserves zero-permission access |
| Mesa Turnip Vulkan Driver | **IN PROGRESS** | Driver bundled as `libvulkan.purple.so`; runtime log pending | Turnip T30 via AdrenoTools |
| Frame Rate Target (40 FPS) | **IN PROGRESS** | Target 40 FPS / 25 ms frame pacing pinned via `DM_FPS_PIN=40` | Benchmarking pending launch |
| Physical Memory Budget | **IN PROGRESS** | Budget $\le 3.2$ GiB PSS, ceiling 4.0 GiB via `DM_APP_POOL=3500M` | Profiling pending launch |
| Video Decoding | **IN PROGRESS** | Cutscenes in MP4 AVC container mapped to `OMX.qcom.video.decoder.avc` | MediaCodec verification pending |
| Survival Gameplay Validation | **IN PROGRESS** | Target: $\ge 10$ continuous minutes of real play | Pending deployment completion |
