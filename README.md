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

| Metric | Before Optimization | After Optimization (30 & 60 FPS Runs) | Delta / Status |
| :--- | :--- | :--- | :--- |
| **Total Process PSS** | **4,054,167 KB (~4.05 GB)** | **3,279,894 – 3,404,091 KB (~3.13 – 3.25 GB)** | **-650 to -774 MB (Strictly < 3.5 GB)** |
| **Total Process RSS** | **> 3.95 GB** | **2,955,520 – 3,470,604 KB (~2.82 – 3.31 GB)** | **Full 3.285 GB Switch 1 Pool Intact** |
| **Zero-Copy Pool (`EGL mtrack`)** | 2,350,000 KB (~2.35 GB) | **276,968 KB (~270 MB)** | **-2.08 GB (-88% via Lazy Realization)** |
| **Swap Usage (`SwapPss`)** | 830,000 KB (~830 MB) | **1,511 KB (~1.5 MB at 30,000 frames @ 60 FPS)** | **-828 MB (Zero Swap Thrashing)** |
| **Replay + Submit / GPU Time** | 12+ ms (stalling on evictions) | **6.14 ms replay / 10.66 ms GPU p50 (@ 60 FPS)** | **16.67 – 18.58 ms Frame Time (54–60 FPS)** |
| **System Stability / LMK** | `lowmemorykiller: Kill (oom_adj 0)` | **0 Kills, 0 Crashes (57,900+ frames @ 30 / 30,177+ @ 60)** | **100% Stable Across Chambers** |
| **Audio Performance** | Buffer underruns on swap lag | **0 Underruns (394,000+ callbacks)** | **Clean Audio Output** |
| **Frame Rate** | Unstable / crashing | **30 FPS or 60 FPS Configurable** (`env.txt`) | **Exceptional Frame Pacing** |

### 2. Live Telemetry from Device Logs (60 FPS Mode, Frame 30,000)

```text
[dm] [vk] custom driver loaded: libcustom_vulkan.so (PurpleVK Adreno 630, Vulkan 1.4.359)
[dm] [vk] memory pool 0x70d31d7000 +0x3a00000: zero-copy (zero-copy 58.0 MB, copied 0.0 MB)
[dm] [vk] memory pool 0x70d1fd6000 +0x1200000: zero-copy (zero-copy 76.0 MB, copied 0.0 MB)
[dm] [vk] memory pool 0x70c79d5000 +0xa600000: zero-copy (zero-copy 242.0 MB, copied 0.0 MB)
[dm] [vk] replay up to present 30000: draws 21447577, skipped 0, passes 747602, clears folded 88599
[dm] [vk] replay per frame (last 600): 498 draws, replay 6.14 ms, GPU busy 10.58 ms; descriptor sets reused 19412169
[dm] [vk] images: 191 live, 838 MB of textures, 634 evicted so far (a newer texture over their memory)
[dm] [nvn] timing up to frame 30000: frame time p50 18.58 / p95 20.12 ms; GPU p50 10.66 / p95 11.44 ms
```

---

## Memory Architecture & Key Optimizations

### 1. On-Demand Zero-Copy Pool Realization (`vk_pool_buf`)
- **The Problem:** Previously, `mtl_pool_add()` eagerly allocated an `AHardwareBuffer` (`DMA-BUF`) and `VkBuffer` for every single `NVNmemoryPool` at initialization (`pool_zero_copy`), faulting all pages upfront via `memcpy` and pinning **2.35 GB** of unswappable physical RAM in `EGL mtrack` (`imported_mem`)—even for shader pools, descriptor pools, and texture-only pools whose contents live in `VkImage` allocations.
- **The Solution:** Pool chunks are now registered as lightweight virtual ranges and only realized into `AHardwareBuffer` + `VkBuffer` allocations on demand (`vk_pool_buf` / `vk_pool_use_buffer`) when bound as GPU buffers (`NVNbuffer` or linear/buffer textures). In *Hades II*, this reduced `EGL mtrack` from **2.35 GB down to 270 MB (-2.08 GB)** while keeping the full 3,285 MB Switch 1 virtual memory pool intact.

### 2. Working-Set-Protected LRU Texture Eviction & Streamed Ground-Texture Integrity
- **The Problem:** Earlier revisions either deadlocked on `im->last_use <= vk_fence_done()` (hoarding >1 GB of dead room textures), evicted GPU-written (`im->gpu_w`) or non-packaged pooled textures (`im->pool` without `im->packaged`), or missed concurrent `ReadFile` / page-fault writes when streaming 16 MB `4096x4096` BC7 ground atlases across chamber transitions.
- **The Solution:**
  - `tex_evictable()` now strictly restricts eviction to clean CPU-packaged textures (`im->packaged && !im->gpu_w && !im->lin_mem`) idle for at least 120 frames, with a 950 MB default chamber working-set budget (`DM_VK_TEX_MAX_MB=950`, `DM_VK_TEX_KEEP_FRAMES=600`).
  - `pkg_upload_()` captures `nvn_dirty_now()` prior to write-protecting and deswizzling guest pages, `nvn_dirty_protect()` waits on in-flight `PG_BUSY` transitions, `release_pages()` stamps page generations on host write-prep even for already-writable (`PG_NONE`) spans, and `h_fs_ReadFile` re-stamps destination pages upon `pread()` completion so streamed ground textures always re-upload cleanly once disk reads finish.

### 3. Submission Ring Buffer Trimming (`NSLOT = 3`)
- Submission slots in `gfx_vk_core.c` were reduced from 6 to 3 (triple buffering). This cuts in-flight deferred resource retention, command buffer duplication, and descriptor pool allocations in half, doubling recycling speed without stalling the rendering pipeline.
- Excess scratch memory blocks (`s->nblk > 2`) are automatically freed during `slot_recycle()`.

### 4. Immediate Kernel Page Reclamation (`MADV_DONTNEED`)
- Hooked `madvise(..., MADV_DONTNEED)` into `SetHeapSize` (SVC 0x01) and `mtl_pool_del()` / `pool_copy_back()`. When guest heaps shrink or NVN memory pools are finalized, unused physical pages are immediately relinquished back to the Linux kernel rather than copied back into dirty anonymous memory or swap.

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
# Frame rate and thermal control
DM_FPS_TARGET=30
DM_FPS_PIN=30
DM_PRESENT_HZ=30

# Vulkan texture cache budget & eviction thresholds
DM_VK_TEX_MAX_MB=950
DM_VK_TEX_KEEP_FRAMES=600
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
