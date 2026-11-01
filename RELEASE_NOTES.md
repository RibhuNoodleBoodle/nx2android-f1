# Release Title: v0.2.1-f1: 6GB RAM Memory Optimization & Ground Texture Fix (Poco F1 Edition)

# nx2android-f1 v0.2.1-f1 — 6GB RAM Milestone Release

This release of **[nx2android-f1](https://github.com/RibhuNoodleBoodle/nx2android-f1)** specializes in **memory-constrained 6 GB RAM devices** (such as the Xiaomi Poco F1, Snapdragon 845 / Adreno 630 on LineageOS Android 15). 

It brings comprehensive memory architectural re-engineering to strictly restrain total app memory consumption to **<= 3.5 GB** without relying on unstable or thrashing zRAM expansions, while fully preserving Switch 1's original **3.2 GB virtual memory pool semantics**.

---

## 🚀 Key Highlights & Breakthroughs

### 1. On-Demand Zero-Copy Pool Realization (`-2.08 GB EGL mtrack`)
- **Before:** All memory pools were eagerly realized upfront into `AHardwareBuffer` (`DMA-BUF`) allocations, pinning **2.35 GB** of unswappable physical RAM in `EGL mtrack` (`imported_mem`) even for shader, descriptor, and texture-only pools.
- **After:** Pools are registered as lightweight virtual ranges and realized into `AHardwareBuffer` + `VkBuffer` allocations strictly on demand (`vk_pool_buf` / `vk_pool_use_buffer`) when bound as GPU buffers.
- **Impact:** `EGL mtrack` dropped from **2.35 GB to 270 MB (-88%)**, freeing **2.08 GB of physical RAM**.

### 2. Streamed Ground-Texture Integrity & Dirty-Page Synchronization
- **Fixed:** Missing chamber ground textures (16 MB `4096x4096` BC7 atlases) caused by:
  - Evicting GPU-written (`im->gpu_w != 0`) and non-packaged pooled textures (`im->pool` without `im->packaged`).
  - Overlap eviction races where dropping an old chamber's texture stripped write protection (`nvn_dirty_release`) from active textures.
  - Concurrency gaps where the replay thread uploaded textures during slow asynchronous disk reads (`pread()` in `h_fs_ReadFile`).
- **Fix:**
  - `tex_evictable()` now strictly permits eviction of clean, idle CPU-packaged textures (`im->packaged && !im->gpu_w && !im->lin_mem`).
  - `pkg_upload_()` takes dirty-page timestamps prior to write-protecting and deswizzling.
  - `release_pages()` and `h_fs_ReadFile()` re-stamp memory spans after file reads complete so newly streamed ground tiles are always uploaded cleanly.

### 3. Working-Set Protected LRU Texture Eviction
- Tuned the default texture working-set ceiling to `DM_VK_TEX_MAX_MB=950` and `DM_VK_TEX_KEEP_FRAMES=600` (~20s).
- Active chamber ground atlases and sprites are protected against mid-room eviction, while textures from previous biomes and rooms are reclaimed automatically via `slot_recycle()`.

### 4. Triple Buffering Submission Ring (`NSLOT = 3`)
- Reduced submission slots from 6 to 3 in `gfx_vk_core.c`.
- Cuts in-flight deferred resource retention, command buffer duplication, and descriptor pool allocations in half, doubling recycling speed without stalling rendering pipelines.

### 5. Immediate Kernel Page Reclamation (`MADV_DONTNEED`)
- Hooked `madvise(..., MADV_DONTNEED)` into guest heap shrinkage (`SetHeapSize`) and pool destruction (`mtl_pool_del()`).
- Unused physical memory is immediately relinquished to the Linux kernel rather than lingering in dirty anonymous pages or swap.

---

## 📊 Live Poco F1 Telemetry & Benchmarks (*Hades II*)

Tested live across extended gameplay sessions (30 FPS and 60 FPS modes):

| Metric | Upstream / Baseline | nx2android-f1 v0.2.1 | Delta / Outcome |
| :--- | :--- | :--- | :--- |
| **Total Process PSS** | **> 4.05 GB** | **3.13 – 3.25 GB** | **-774 to -916 MB (Strictly < 3.5 GB)** |
| **Total Process RSS** | **> 3.95 GB** | **2.82 – 3.31 GB** | **Full 3.285 GB Switch 1 Pool Intact** |
| **Zero-Copy Pool (`EGL mtrack`)** | 2,350 MB (~2.35 GB) | **270 MB** | **-2.08 GB (-88% Physical RAM Savings)** |
| **Swap Usage (`SwapPss`)** | 830 MB | **~1.5 MB** | **-828 MB (Zero Swap Thrashing)** |
| **Replay CPU Time** | 12+ ms (stalling) | **5.19 – 6.14 ms** | **Fast CPU Frame Submission** |
| **GPU Frame Time (@ 60 FPS)** | Severe stutter | **10.66 ms p50 / 11.44 ms p95** | **Smooth 54–60 FPS Gameplay** |
| **Stability / LMK Kills** | Killed after 2–3 chambers | **0 Kills, 0 Crashes (57,900+ frames @ 30 / 30,000+ @ 60)** | **100% Rock Solid** |
| **Audio Performance** | Buffer underruns on swap lag | **0 Underruns (394,000+ callbacks)** | **Crisp, Glitch-Free Audio** |

---

## 📦 Package Contents

- **`nx2android-v0.2.0-18-g0ef91bd-windows-x64.zip` / `release.zip`:**
  - Standalone Windows 64-bit desktop GUI and CLI tool (`nx2android.exe`).
  - Bundled `nxnative/` engine and profiles including all memory and Vulkan fixes.
  - No copyrighted game files or keys included. Bring your own `prod.keys` and dumped Switch games.

## 🛠 Recommended Device Settings (`env.txt`)

For Snapdragon 845 / 6 GB RAM devices, place in `/sdcard/Android/data/<package>/files/env.txt`:

```ini
# Frame rate & timing target (30 or 60)
DM_FPS_TARGET=60
DM_FPS_PIN=60
DM_PRESENT_HZ=60

# Texture cache budget for chamber working sets
DM_VK_TEX_MAX_MB=950
DM_VK_TEX_KEEP_FRAMES=600
DM_VK_IMG_BLOCK_MB=4
```
