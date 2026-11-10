# The Long Dark — Content Matrix

## 1. Content Inventory & Switch Version Map

| Component | Switch Title ID | Content Type | Version | Source File | Size (Bytes) | Status / Integration Point |
|---|---|---|---|---|---|---|
| **The Long Dark (Base Game)** | `01007A700A87C000` | Application (Base) | `v0` (1.0.0) | `The Long Dark [01007A700A87C000] [v0].nsp` | 7,729,444,752 | Available / Extracted base ExeFS + RomFS |
| **Switch Update v983040** | `01007A700A87C800` | Patch (Update) | `v983040` (`0xF0000`) | `The Long Dark Switch NSP Update v983040.nsp` | 16,198,403,951 | Available / Overlaid ExeFS + RomFS update (includes WINTERMUTE Episodes 1-4 & June 2026 hotfixes) |
| **Tales from the Far Territory** | `01007A700A87D001` | AddOnContent (DLC) | `v262144` (`0x40000`) | `The Long Dark [DLC Tales from the Far Territory][01007A700A87D001][v262144].nsp` | 395,794,640 | Available / AOC RomFS integration for Far Territory expansion |
| **WINTERMUTE Story Mode** | Included in Base/Update | Game Mode | Story Episodes 1-4 | Embedded in RomFS (`/Data/StreamingAssets/Video/Switch/main_titles_ep*.mp4`) | N/A (in update RomFS) | Available / Validated via Story Mode menu and intro cinematics |
| **Iron Road** | N/A | PC Early Access DLC | N/A | Not released on Switch | N/A | **NOT on Switch** (Steam-only Early Access released Oct 27, 2026; strictly excluded) |

---

## 2. Update Precedence and Dependency Order

1. **Base Layer (`v0`):** Provides the foundational Unity 2019/2020 IL2CPP runtime structure, base asset archives, and default configuration.
2. **Update Layer (`v983040`):** Highest official Switch patch provided in user dump (`0xF0000`). Overrides `main` IL2CPP executable (61.4 MB) and `sdk` (6.2 MB), supplying updated engine code, updated shader libraries, and asset patches for Survival Mode and WINTERMUTE.
3. **DLC Layer (`01007A700A87D001`):** Registered AddOnContent mounted at runtime via Horizon `aoc` service simulation.

---

## 3. Middleware & Engine Analysis

- **Engine:** Unity (IL2CPP 64-bit ARM64)
- **Audio Middleware:** Audiokinetic Wwise (Soundbanks in `/Data/StreamingAssets/Audio/GeneratedSoundBanks/Switch/`, audio media `.wem`)
- **Video Decoding:** Hardware H.264 / AVC MP4 containers (`/Data/StreamingAssets/Video/Switch/intro_cards.mp4`, `main_titles.mp4`, etc.) routed via Qualcomm `OMX.qcom.video.decoder.avc` via Android `AMediaCodec`.
- **Renderer:** NVN translated to Vulkan targeting Mesa Turnip (Adreno 630).
- **Target Frame Rate:** 40 FPS presentation cap (25 ms frame pacing).
