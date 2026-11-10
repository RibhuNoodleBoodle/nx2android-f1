# Storage Layout & Removable MicroSD Architecture

## 1. Storage Volume Identification (Poco F1)

- **Device:** Xiaomi Poco F1 (`beryllium`, Snapdragon 845)
- **OS:** LineageOS 22.2 (Android 15, API level 35)
- **Removable MicroSD Volume UUID:** `EA2B-B2FD`
- **MicroSD Mount Point:** `/storage/EA2B-B2FD`
- **Filesystem Format:** exFAT (`fs=exfat`, `errors=remount-ro`)
- **Capacity & Free Space:** 59 GB Total / 41 GB Free (31% used)
- **Internal Storage (Emulated):** `/storage/emulated/0` (`/sdcard`) — 52 GB Total / 20 GB Free

---

## 2. Directory Hierarchy & Paths

The Long Dark requires ~16–18 GB of bulk RomFS asset data. Placing this on internal storage risks exhausting system storage and causing OS instability. Assets are structured directly in the app-specific removable volume directory:

### Package Name
`org.nx2a.t01007a700a87c000` (derived from Switch Title ID `01007A700A87C000`)

### Target Removable Asset Hierarchy
```
/storage/EA2B-B2FD/
└── Android/
    └── data/
        └── org.nx2a.t01007a700a87c000/
            └── files/
                ├── romfs/
                │   ├── Data/
                │   │   ├── Managed/
                │   │   │   └── Metadata/
                │   │   │       └── global-metadata.dat
                │   │   ├── Resources/
                │   │   └── StreamingAssets/
                │   │       ├── Audio/
                │   │       └── Video/Switch/*.mp4
                └── aoc/
                    └── 01007A700A87D001/  (Tales from the Far Territory DLC)
```

### Internal High-Speed Storage
- **Native Executables & Libraries:** `/data/app/.../lib/arm64/` (`libdmhost.so`, `vulkan.turnip.so`, `libadrenotools.so`)
- **Game Saves & Settings:** `/data/user/0/org.nx2a.t01007a700a87c000/files/saves/` and `settings.txt` (kept in private app storage for POSIX atomic fsync guarantees and zero-corruption during power-loss).

---

## 3. Android Permission Model

1. **App-Specific Directory Sandbox (Scrubbed Manifest):**
   - Under Android API 29 through 35, directories under `/storage/<UUID>/Android/data/<package>/` are private to the owning package (UID-scoped `sdcardfs` / `fuse` projection).
   - The application does NOT require or declare `MANAGE_EXTERNAL_STORAGE` or `READ_EXTERNAL_STORAGE` for its own app-specific data directory.
   - When deployed via ADB push, the destination is created with matching ownership (`u0_aXXX:everybody`).

2. **Network & Bluetooth Restriction:**
   - Manifest declares zero network permissions (`android.permission.INTERNET`, `ACCESS_NETWORK_STATE`, `BLUETOOTH` are completely omitted).
   - Fully standalone offline operation.
