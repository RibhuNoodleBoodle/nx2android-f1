# Upstream Native Core Integration Ledger

- **Date:** 2026-11-09
- **Base Fork:** https://github.com/RibhuNoodleBoodle/nx2android-f1
- **Base Commit SHA:** `57b4fe8013d9d309d45a10aad8bf212758b71b23`
- **Upstream Repository:** https://github.com/shirai91/nx2android
- **Upstream Commit SHA:** `e9bd2b764608facf841e39292d868ee657eaf634`
- **Integration Scope:** `nxnative/` core only; preserving all Poco F1 / Adreno 6xx / Android Turnip generic fixes.

---

## 1. Merged Improvements from Upstream

1. **Shader AST Optimization (`AstOptimizer.cs`)**:
   - Imported upstream fix preventing propagation of expressions reading writable memory (`ReadsWritableMemory`) past stores.
   - Prevents memory order hazards in compute shaders and radix sorting.
2. **Horizon Play Statistics Service (`hle_offline.c`)**:
   - Added `_ZN2nn3pdm30QueryApplicationPlayStatistics...` stub answering 0 play time / 0 launches instead of crashing with SIGSEGV when games query play statistics.
3. **Audio Renderer SDK Version Gate & Catch-Up Tracing (`hle_audren.c`)**:
   - Added `!DM_SDK_OLD(dm_sdk_major())` gate for audio event layout check (protecting SDK 10 and older from invalid event layout assumptions).
   - Added `DM_AUDREN_TRACE` catch-up frame latency and thread logging.
4. **Guest Thread Yield Watchdog (`hle_thread.c`)**:
   - Integrated `yield_watch` to detect and diagnose spin/yield deadlocks in guest threads across long loops.
5. **Shader Splitter & Binder Updates (`shader_split.py`, `shader_vk.py`, `dmsv.py`)**:
   - Added `unsized` decompression for HAL `.cmp` container formats in `shader_split.py`.
   - Corrected storage buffer write flag bit test `s['flags'] & 1` in `shader_vk.py`.
   - Imported robust atomic writing and exception handling in `dmsv.py` to prevent Windows file locking issues during shader bundle generation.
6. **APK Modular Signing & Driver Replacement Tool (`apkdriver.py`, `apksign.py`)**:
   - Integrated `apkdriver.py` and standard `apksign.py` module for APK rebuilds and signature scheme v2 validation.
7. **Movie AAC Audio MPEG Speaker Ordering (`aac_layout.h`, `audioconverter_mediacodec.c`)**:
   - Integrated optional AAC MPEG channel ordering for multi-channel video decoders.
8. **UI & Pause Menu Enhancements (`front_android.c`, `overlay_layout.h`, `font5x7.h`)**:
   - Integrated dynamic `Face buttons` controller remap row in the pause menu (`DM_PAD_LABEL`) allowing in-game toggling between physical button labels and Nintendo positional mapping.
   - Integrated battery and thermal monitoring hooks.
   - Integrated Spanish text and Latin-1 glyph rendering in overlay.

---

## 2. Fork-Specific Behavior & Poco F1 Optimizations Retained

1. **Hardware Memory Alignment in ELF Loader (`loader.c`)**:
   - Retained dynamic `getpagesize()` alignment (4 KB on Linux/Android) in `loader.c` instead of hardcoded 16 KB masks, preserving writable data pages from accidental execute-only protection.
2. **Adreno 630 / Turnip Texture Cache & Direct Readback (`gfx_vk_tex.c`, `hle_nvn.c`)**:
   - Retained direct CPU-written memory deswizzling in `mtl_read_texels` for virtual texturing and fast readback.
   - Retained texel alignment fallback avoiding 1.05 MB per-draw scratch buffer allocation in `vk_tbuf_view`.
   - Retained compressible texture upload handling for uncompressed formats in `cpu_content_tex`.
   - Retained data cache flushing (`sys_dcache_flush`) for GPU-to-CPU host coherency.
3. **Poco F1 Memory Ceiling & Zero-Copy Realization**:
   - Retained `DM_APP_POOL` ceiling constraints and memory pool recycling optimizations.
4. **Strict Offline Single-Player Execution**:
   - Retained strict omission of `android.permission.INTERNET`, `ACCESS_NETWORK_STATE`, and `BLUETOOTH` in `mkapk.py` and merged manifests.

---

## 3. Pre/Post-Merge Verification

- `python -m porter doctor`: PASS (All tools, NDK 27c, .NET SDK 10, adb, prod.keys ok).
- Unit tests `tests.test_install` & `tests.test_porter`: PASS (44 tests OK).
