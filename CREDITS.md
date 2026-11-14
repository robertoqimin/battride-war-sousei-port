# Credits and licenses

This project exists thanks to the work of the people below. Each part keeps its license in its own files.

## Foundation of the port

### bbport: native PC port of Bloodborne
- <https://github.com/deadinside28/bloodborne_pc> (forked at commit `0fc2921`)
- Copyright 2026 bbport contributors. **GPL-2.0-or-later.**
- The whole `port/` directory comes from bbport:
  - the HLE runtime in `src/`;
  - the GPU layer and shader translator in `gpu/`;
  - the preparation and link scripts, the tests and `build.sh`.
- bbport's original notice is kept in `port/NOTICE.bbport`. This project's changes are marked with `kr-port:` comments.
- Left out of this fork because this game does not use them: bbport's Bloodborne profile and hooks (`gpu/games/bloodborne*`) and its online module (`gpu/bbnet20`, with the cpp-httplib and wepoll libraries only it used).

### shadPS4: PS4 emulator
- <https://github.com/shadps4-emu/shadPS4>
- Copyright shadPS4 Emulator Project. **GPL-2.0-or-later.**
- bbport's GPU layer started from shadPS4's video core and shader recompiler.
- A few functions of this port were also ported from shadPS4: Rtc, trophies and DiscMap. Their origin is marked where the code is used.

## Third-party libraries (each under the license in its own folder)

| Library | Location | License |
|---|---|---|
| LibAtrac9 (Alex Barney): the ATRAC9 decoder used by the audio mixer | `port/third_party/LibAtrac9` | MIT |
| sirit (SPIR-V emitter) | `port/gpu/third_party/sirit` | BSD-3-Clause |
| Dear ImGui | `port/gpu/third_party/imgui` | MIT |
| AMD FidelityFX (FSR 1/3, FFX API) | `port/gpu/third_party/amd-fsr1`, `ffx-api-2.4`, `fsr-vulkan` | MIT |
| AMD GCN/PM4 headers (PAL) | `port/gpu/third_party/gcn` | MIT (AMD) |
| SMAA | `port/gpu/third_party/smaa` | MIT |
| half | `port/gpu/third_party/half` | MIT |
| DejaVu fonts | `port/gpu/third_party/fonts` | Bitstream Vera / DejaVu license |
| DLSS bridge (from IFreemz/shadPS4-Bloodborne-DLSS-FSR) | `port/gpu/dlss_bridge` | MIT |

Used at build time: SDL3 (zlib), Vulkan, Vulkan Memory Allocator and xbyak.

## References consulted
- **SpartanPort** and the **shadPS4-Infamous-FixBuild** fork: consulted to compare approaches; no code was copied.
- Ghidra (NSA), used to analyze the executable from the user's own dump.

## This project
- **robertoqimin**, assisted by Claude (Anthropic). Original work includes:
  - the Ngs2 mixer with ATRAC9;
  - the in-memory 60 FPS patch;
  - the offline stubs;
  - the game's control layout;
  - the hash-based rwlock registry;
  - the benchmark tooling.
- Files outside `port/` are under **GPL-2.0-only** (`LICENSE`). Inside `port/`, **GPL-2.0-or-later** applies (`port/LICENSE`).

## The game
*Kamen Rider: Battride War Genesis* (仮面ライダー バトライド・ウォー 創生) © Bandai Namco Entertainment, © Ishimori Productions / Toei. This project is **not** affiliated with Bandai Namco, Toei or Sony Interactive Entertainment and contains **no** game code or data.
