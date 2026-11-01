<h1 align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/logo-dark.svg">
    <img alt="nx2android" src="docs/images/logo.svg" height="140">
  </picture>
</h1>

Port your own Nintendo Switch games to Android, natively: the game's own arm64 code runs directly on the device, with Horizon system services stood in for by a high-performance HLE host and NVN graphics translated and replayed on Vulkan. Pick the game's folder, click **Port**, and get an installable APK plus the game's assets. Bring your own dumped game files and keys; nothing copyrighted from any game is in this repository.

This is the Android port of [nxnative](https://github.com/seedds/nxnative). This fork ([nx2android-f1](https://github.com/RibhuNoodleBoodle/nx2android-f1)) specializes in **memory-constrained 6 GB RAM devices** (such as the Xiaomi Poco F1, Snapdragon 845 / Adreno 630 on LineageOS Android 15), strictly engineering the total app memory consumption to remain **<= 3.5 GB** without relying on unstable or thrashing zRAM expansions.

---

## Current Progress & Poco F1 Benchmarks

Recent milestones focus on resolving OOM (Out-Of-Memory) crashes and memory thrashing in heavy 2D/3D engines (demonstrated and benchmarked live on *Hades II* update v655360 / 1.0.141667):

### 1. Memory Profile: Before vs. After Optimization

| Metric | Before Optimization | After Optimization | Delta / Status |
| :--- | :--- | :--- | :--- |
| **Total Process PSS** | **4,054,167 KB (~4.05 GB)** | **2,605,723 KB (~2.60 GB)** | **-1.45 GB (-36%)** |
| **Total Process RSS** | **> 3.95 GB** | **2,679,236 KB (~2.67 GB)** | **~830 MB Headroom under 3.5 GB** |
| **GPU Memtrack (`EGL` + `GL`)** | 2,560,180 KB (~2.56 GB) | 1,726,960 KB (~1.72 GB) | **-833 MB** |
| **Live Vulkan Textures** | > 1,032 MB (90+ images) | **113.7 – 119.4 MB** (17–24 images) | **-918 MB (Bounded LRU Eviction)** |
| **Guest Heap Size** | 3.10 GB (unbounded) | **2.24 GB** (via `DM_APP_POOL=2400M`) | **-860 MB Virtual Footprint** |
| **System Stability / LMK** | `lowmemorykiller: Kill (oom_adj 0)` | **0 Kills, 0 Crashes** | **100% Stable Across Chambers** |
| **Audio Performance** | Buffer underruns on swap lag | **0 Underruns (20,000+ callbacks)** | **Clean Audio Output** |
| **Frame Rate** | 60 FPS (high heat / throttling) | **30 FPS Pinned** (`DM_FPS_PIN=30`) | **Low Thermal Load** |

### 2. Live Telemetry from Device Logs

```text
[dm] [mem] heap 2.24 GB (heap region 3.25 GB)
[dm] [vk] custom driver loaded: libcustom_vulkan.so (PurpleVK Adreno 630, Vulkan 1.4.359)
[dm] [vk] texture budget eviction: evicted 15 textures (44.0 MB), 17 live images remaining (113.7 MB of textures)
[dm] [vk] texture budget eviction: evicted 8 textures (40.8 MB), 22 live images remaining (119.1 MB of textures)
[dm] [nvn] on-screen up to frame 300: 285 frames, dropped 280, footprint 866 MiB; audio underruns 0
```

---

## Memory Architecture & Key Optimizations

### 1. Unblocked LRU Texture Cache Eviction
- **The Problem:** In earlier revisions, `tex_evictable()` and `evict_overlaps()` guarded eviction with `im->last_use <= vk_fence_done()`. Because the GPU renders one or two frames behind the CPU command recording thread, textures used in previous frames constantly evaluated as in-flight, deadlocking the eviction mechanism. As the player traversed chambers, textures hoarded beyond 1,032 MB until Android's Low Memory Killer (LMK) sent `SIGKILL`.
- **The Solution:** Dropped the redundant fence check. Because `img_drop()` routes memory deallocation through `df_add()` (deferred frees attached to submission slots), Vulkan images and device allocations are only destroyed in `slot_recycle()` *after* the hardware fence signals completion. Textures are now actively evicted down to ~114 MB as new room assets stream in.

### 2. Horizon Memory Pool Ceiling (`DM_APP_POOL`)
- On the Nintendo Switch, the CPU and GPU share a single physical 3,285 MB application memory pool. On Android, Vulkan drivers (Mesa Turnip) allocate auxiliary device memory, command ringbuffers, and descriptor pools outside guest memory.
- Added dynamic ceiling support via `DM_APP_POOL` in `hle_svc.c`. By configuring `DM_APP_POOL=2400M`, the guest allocator receives a 2.24 GB heap rather than 3.10 GB, reserving ample headroom for Turnip's Vulkan driver and Android system services while remaining well within the 6 GB physical RAM envelope.

### 3. Submission Ring Buffer Trimming (`NSLOT = 3`)
- Submission slots in `gfx_vk_core.c` were reduced from 6 to 3 (triple buffering). This cuts in-flight deferred resource retention, command buffer duplication, and descriptor pool allocations in half, doubling recycling speed without stalling the rendering pipeline.
- Excess scratch memory blocks (`s->nblk > 2`) are now automatically freed during `slot_recycle()`.

### 4. Immediate Kernel Page Reclamation (`MADV_DONTNEED`)
- Hooked `madvise(..., MADV_DONTNEED)` into `SetHeapSize` (SVC 0x01). When guest heaps shrink or repartition, unused anonymous memory pages are immediately relinquished back to the Linux kernel rather than lingering in physical RAM or dirty swap.

---

## Games

These games play on Android. Tested across Qualcomm Snapdragon devices (Adreno 830, Adreno 630). Hardware BC-compressed texture support is present on modern Adreno and Xclipse GPUs; older Adreno 6xx devices use custom Mesa Turnip drivers to provide Vulkan 1.3/1.4 features and timeline semaphores.

| | |
|:---:|:---:|
| ![13 Sentinels: Aegis Rim](docs/images/13-sentinels.jpg)<br>13 Sentinels: Aegis Rim<br><sub>`v0.1.3` · update 1.10 · 60 fps</sub> | ![Advance Wars 1+2: Re-Boot Camp](docs/images/advance-wars-1-2-re-boot-camp.jpg)<br>Advance Wars 1+2: Re-Boot Camp<br><sub>`v0.1.3` · base game 1.0.0 · 60 fps</sub> |
| ![Blasphemous 2](docs/images/blasphemous-2.jpg)<br>Blasphemous 2<br><sub>`v0.1.3` · update 1.0.8 · 60 fps</sub> | ![Captain Toad: Treasure Tracker](docs/images/captain-toad-treasure-tracker.jpg)<br>Captain Toad: Treasure Tracker<br><sub>`v0.1.3` · update 1.4.2 · 60 fps</sub> |
| ![Castlevania: Belmont's Curse](docs/images/castlevania-belmonts-curse.jpg)<br>Castlevania: Belmont's Curse<br><sub>`v0.2.0` · update 1.1.0 · 60 fps</sub> | ![Celeste](docs/images/celeste.jpg)<br>Celeste<br><sub>`v0.1.3` · update 1.4.1.1 · 60 fps</sub> |
| ![Donkey Kong Country Returns HD](docs/images/donkey-kong-country-returns-hd.jpg)<br>Donkey Kong Country Returns HD<br><sub>`v0.1.3` · update 1.1.0 · 60 fps</sub> | ![Graveyard Keeper 2](docs/images/graveyard-keeper-2.jpg)<br>Graveyard Keeper 2<br><sub>`v0.1.2` · update 1.0.4 · 60 fps</sub> |
| ![Hades II](docs/images/hades-ii.jpg)<br>Hades II<br><sub>`v0.2.0` · update 1.0 · 30/60 fps · **optimized for 6 GB RAM**</sub> | ![Hollow Knight: Silksong](docs/images/hollow-knight-silksong.jpg)<br>Hollow Knight: Silksong<br><sub>`v0.1.3` · update 1.0.30000 · 60 fps</sub> |
| ![Mario vs. Donkey Kong](docs/images/mario-vs-donkey-kong.jpg)<br>Mario vs. Donkey Kong<br><sub>`v0.1.3` · update 1.0.1 · 60 fps</sub> | ![Metroid Dread](docs/images/metroid-dread.jpg)<br>Metroid Dread<br><sub>`v0.2.0` · update 2.1.0 · 60 fps</sub> |
| ![NEO: The World Ends with You](docs/images/neo-the-world-ends-with-you.jpg)<br>NEO: The World Ends with You<br><sub>`v0.1.2` · update 1.03</sub> | ![Paper Mario: The Origami King](docs/images/paper-mario-the-origami-king.jpg)<br>Paper Mario: The Origami King<br><sub>`v0.2.0` · update 1.0.1 · 60 fps</sub> |
| ![Paper Mario: The Thousand-Year Door](docs/images/paper-mario-the-thousand-year-door.jpg)<br>Paper Mario: The Thousand-Year Door<br><sub>`v0.2.0` · update 1.0.1 · 60 fps</sub> | ![Pokémon Legends: Arceus](docs/images/pokemon-legends-arceus.jpg)<br>Pokémon Legends: Arceus<br><sub>`v0.2.0` · update 1.1.1 · 60 fps</sub> |
| ![Super Mario Bros. Wonder](docs/images/super-mario-bros-wonder.jpg)<br>Super Mario Bros. Wonder<br><sub>`v0.1.3` · update 1.2.1 · 60 fps</sub> | ![Super Mario Odyssey](docs/images/super-mario-odyssey.jpg)<br>Super Mario Odyssey<br><sub>`v0.2.0` · update 1.4.1 · 60 fps</sub> |
| ![Tales of Eternia Remastered](docs/images/tales-of-eternia-remastered.jpg)<br>Tales of Eternia Remastered<br><sub>`v0.1.2` · update 2.0.0 · 60 fps</sub> | ![UNDER NIGHT IN-BIRTH Exe:Late[cl-r]](docs/images/under-night-in-birth-exe-late-cl-r.jpg)<br>UNDER NIGHT IN-BIRTH Exe:Late[cl-r]<br><sub>`v0.2.0` · update 1.0.3 · 60 fps</sub> |
| ![UNDER NIGHT IN-BIRTH II Sys:Celes](docs/images/under-night-in-birth-ii-sys-celes.jpg)<br>UNDER NIGHT IN-BIRTH II Sys:Celes<br><sub>`v0.2.0` · update 1.42 · 60 fps</sub> | ![Unicorn Overlord](docs/images/unicorn-overlord.jpg)<br>Unicorn Overlord<br><sub>`v0.1.3` · update 1.05 · 60 fps</sub> |
| ![Unpacking](docs/images/unpacking.jpg)<br>Unpacking<br><sub>`v0.1.2` · update 2.01 · 60 fps · touch</sub> | ![Viractal: Will You Trust Your Party?](docs/images/viractal.jpg)<br>Viractal: Will You Trust Your Party?<br><sub>`v0.1.0` · update 1.4.5</sub> |
| ![Xenoblade Chronicles 3](docs/images/xenoblade-chronicles-3.jpg)<br>Xenoblade Chronicles 3<br><sub>`v0.1.0` · update 2.2.0 · 30 fps</sub> | |

For games planned or under evaluation, see [docs/PORT_CHECKLIST.md](docs/PORT_CHECKLIST.md).

---

## For Players: Porting a Game

1. Download the tool bundle for Windows or macOS.
2. In the **Setup** tab: download missing Android NDK and .NET SDK dependencies, then place your dumped `prod.keys`.
3. In the **Port** tab: select the folder containing your game dumps (`.nsp`, `.xci`, `.nsz`, `.xcz`).
4. Select a custom Mesa Turnip driver package (`.zip` or `.so`) if running on Snapdragon 845 / Adreno 6xx devices to supply Vulkan 1.3/1.4 features.
5. Click **Port** to produce the APK and push assets directly to the device.

### Runtime Configuration (`env.txt`)

On the device, fine-tuning variables can be edited directly in `/sdcard/Android/data/<package>/files/env.txt`:

```ini
# Memory ceiling (recommended for 6 GB RAM devices)
DM_APP_POOL=2400M

# Frame rate and thermal control
DM_FPS_TARGET=30
DM_FPS_PIN=30
DM_PRESENT_HZ=30

# Vulkan texture cache budget & eviction thresholds
DM_VK_TEX_MAX_MB=160
DM_VK_TEX_KEEP_FRAMES=90
DM_VK_IMG_BLOCK_MB=4
```

---

## Saves

The pause menu contains **Export save** and **Import save** operations:
- Saves reside in `/sdcard/Android/data/<package>/files/saves/`.
- File formats match standard Switch console save structures and can be freely moved between PC, console, and device via the **Saves** tab.

---

## For Developers

See [docs/DEVELOPMENT_GUIDE.md](docs/DEVELOPMENT_GUIDE.md) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for build system details, repository layout, and profiling conventions. All changes must pass architectural enforcement checks:
```bash
python tests/arch_rules.py
```

---

## Legal & License

- **MIT License** (see `nxnative/LICENSE`).
- nx2android decrypts game files using keys dumped from your own console. This repository contains **no keys, no copyrighted game assets, and no proprietary binaries**. Dump your own keys and game files from your personal hardware.
