<div align="center">

# Battride War Genesis

**Kamen Rider: Battride War Genesis (PS4) as a native Linux program, from your own dump.**

[![Latest release](https://img.shields.io/github/v/release/robertoqimin/battride-war-sousei-port?label=latest)](https://github.com/robertoqimin/battride-war-sousei-port/releases/latest)
![Linux](https://img.shields.io/badge/Linux-AppImage-FCC624?logo=linux&logoColor=black)
![Vulkan](https://img.shields.io/badge/renderer-Vulkan-AC162C?logo=vulkan&logoColor=white)
[![License: GPL-2.0](https://img.shields.io/badge/license-GPL--2.0-1baf7a)](LICENSE)

</div>

> [!IMPORTANT]
> - **You need your own dump:** the base game **CUSA02716** v01.00 and its **v01.02** update, decrypted, from
>   your own console. No game files, firmware or keys are included or downloaded by this project.
> - **The game is in Japanese** (text and voices). This port does not translate it.
> - **Experimental:** Story Mode is playable through its first missions. The rest of the game has not been played
>   through yet.
> - Not affiliated with Bandai Namco, Toei or Sony Interactive Entertainment.

The game's own x86-64 code runs directly on your CPU. A runtime written for this game takes the place of the PS4's
system libraries, and the PS4's graphics are translated to Vulkan. It works like Wine with DXVK, built for one game.
The port is a fork of [bbport](https://github.com/deadinside28/bloodborne_pc), the native Bloodborne port, which grew
out of [shadPS4](https://github.com/shadps4-emu/shadPS4).

## Contents

- [Quick start](#quick-start)
- [Requirements](#requirements)
- [Status](#status)
- [Controls](#controls)
- [Display options](#display-options)
- [Performance](#performance)
- [Updating, uninstalling and saves](#updating-uninstalling-and-saves)
- [Troubleshooting](#troubleshooting)
- [Reporting a problem](#reporting-a-problem)
- [For developers](#for-developers)
- [Credits and license](#credits-and-license)

## Quick start

1. Download `BattrideWarGenesis-*-x86_64.AppImage` from the
   [latest release](https://github.com/robertoqimin/battride-war-sousei-port/releases/latest).
2. `chmod +x BattrideWarGenesis-*-x86_64.AppImage` and run it.
3. On the **Game** page, choose the folder of the base game and the folder of the update.
4. Click **Prepare & Play**.

The first launch links your dump into the data folder and prepares the game. It takes a few seconds and happens
once. Nothing in your dump is copied or changed.

> **Getting the dump:** the folders must hold a decrypted (plaintext) `eboot.bin`, which is what current PS4 dumping
> tools produce. Other regions, versions and digital copies have not been tested.

## Requirements

| | |
|---|---|
| **System** | Linux with glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Fedora 36 and newer), FUSE 3 |
| **CPU** | x86-64 with the PS4 CPU's instructions (SSE4.2, POPCNT, AVX, BMI1, LZCNT, MOVBE): Intel Haswell and newer, every AMD Ryzen. The game says so before it starts if one is missing |
| **GPU** | Vulkan 1.3. Tested: AMD Radeon RX 6750 XT (Mesa RADV) and NVIDIA GeForce GTX 1650 (proprietary driver) |
| **Disk** | the dump itself; the data folder only links to it |

Per-core CPU speed matters in crowded fights.

## Status

**Playable through the first Story Mode missions.** Boot, menus, movies, saves, the tutorial and combat work.

What works in the tested parts:

- **Frame rate:** 60 FPS by default (the original caps at 30), up to 240, with the game speed unchanged.
- **Graphics:** render sizes from 720p to 4K, SMAA or FXAA on the 3D scene (the HUD stays sharp), AMD FSR 1.
- **Audio:** music, voices and effects (ATRAC9).
- **Saves:** stored in the data folder.
- **Input:** controllers through SDL (DualShock, DualSense, Xbox), keyboard and mouse with the game's own layout.

**Known issues:**

- Online features report "signed out".
- No reverb, envelopes or mastering in the audio yet.
- A crash in the sound thread was seen once and could not be reproduced.
- No DLSS and no FSR 2 or 3.

**Not validated yet.** Help testing is welcome:

- [ ] the final boss, the ending and the credits
- [ ] Time Survival, Free Mission and the galleries
- [ ] the Steam Deck, other GPUs and other computers

## Controls

This is a Japanese release: **○ confirms and ✕ goes back.** Controllers and keyboard and mouse work together.

| Action | PS4 pad | Keyboard and mouse |
|---|---|---|
| Move / camera | sticks | W A S D / mouse (or numpad 8-4-5-6) |
| Normal attack | □ | left mouse button, J |
| Special 1 | △ | right mouse button, I |
| Special 2 / confirm | ○ | E, L, Enter |
| Jump / Rider Cancel / back | ✕ | Space, K, Backspace |
| Special 3 | △ + ○ | F |
| Rider Glide | □ + ✕ | V |
| Super special | R2 | R, middle mouse button |
| Guard | L1 | Left Shift |
| Switch rider (TAG) | R1 | Tab |
| Pause / next page | OPTIONS | Esc |

- Click in the game window to let the mouse turn the camera.
- **Insert** (or L3 + R3) opens the port's menu during the game.
- **Remapping:** the launcher's **Keyboard** page changes every key; the **Controller** page sets which button each
  PS4 button reads.
- Controller buttons follow their position, not their label: the bottom face button is ✕ on every layout.

## Display options

On the launcher's **Settings** page. Changes apply the next time the game starts.

- **Window:** windowed, borderless fullscreen or exclusive fullscreen.
- **Render resolution:** 720p, 900p, 1080p (the game's own), 1440p or 4K. Above the window's size the frame is filtered down (supersampling).
- **Frame rate:** 30 (the original), 60 (default), 120, 144, 165, 240 or the screen's refresh rate; **VSync** on or off.
- **Anti-aliasing:** SMAA or FXAA on the 3D scene, before the HUD is drawn.
- **Upscaler:** AMD FidelityFX Super Resolution 1.0, or bilinear, when the render resolution is below the window's.
- The picture keeps 16:9, with black bars on other screen shapes.

## Performance

Average frame rate in 60 seconds of combat, with the frame rate uncapped:

| GPU | 720p | 1080p | 1440p | 4K |
|---|---|---|---|---|
| NVIDIA GTX 1650 4 GB | 139 | 98 (SMAA: 89) | 72 | — |
| AMD RX 6750 XT | — | — | holds 60 | 43–55 |

SMAA or FXAA cost about 1 ms per frame on the GTX 1650, and FSR 1 about 1.8 ms (720p with FSR 1: 111 FPS).

## Updating, uninstalling and saves

| | |
|---|---|
| **Data** (the prepared game, saves, logs) | `~/.local/share/battride-war-sousei-port` |
| **Saves** | `user/savedata` in the data folder |
| **Settings** | `~/.config/battride-war-sousei-port/settings.json` |

The data folder can be changed on the launcher's **Game** page.

- **Updating:** download the new release and run it. Saves and settings stay.
- **Uninstalling:** delete the AppImage, then the folders above. **The data folder holds your saves:** copy
  `user/savedata` first if you want to keep them.

## Troubleshooting

### The launcher says the dump is not supported

It checks for CUSA02716: the base game v01.00 and the v01.02 update, each in its own folder, with a decrypted
`eboot.bin`. Choose the folder that holds `eboot.bin` and `sce_sys`.

### The game does not start, or the window stays black

- The launcher's **Log** page shows the game's output, and why it stopped.
- The GPU needs Vulkan 1.3 and a current driver.

### My controller is not recognised

Any controller SDL 3 knows works as the PS4 pad. `game.log` names each controller as it connects.

## Reporting a problem

Open an [issue](https://github.com/robertoqimin/battride-war-sousei-port/issues) with:

- your distribution, CPU, GPU and driver;
- what you did and what happened;
- the logs: the launcher's **Log** page has a button that opens their folder (`prepare.log` and `game.log`).

## For developers

- **Building from source**, playing from a checkout, the environment variables and the benchmark:
  [docs/BUILD.md](docs/BUILD.md).
- **Changes per version:** [CHANGELOG.md](CHANGELOG.md).
- Step-by-step development reports, in Portuguese: [docs/relatorios/](docs/relatorios/). The screenshots they
  mention are not published, because they are game content.

The repository contains only the port's code, scripts, tests and documentation, and no game code or data.

## Credits and license

The people and projects behind this port: [CREDITS.md](CREDITS.md).

Copyright (c) 2026 robertoqimin and the battride-war-sousei-port contributors.

- Files **outside `port/`** are under the **GNU General Public License version 2 only (GPL-2.0-only)**
  ([LICENSE](LICENSE)).
- **`port/`** stays under **GPL-2.0-or-later** ([port/LICENSE](port/LICENSE)), like the bbport and shadPS4 code it
  comes from. Some parts carry other licenses, stated in their own files.

Each release carries the AppImage and `BattrideWarGenesis-*-sources.tar`, the source of every third-party binary
in it. The game is prepared from the player's own dump, and no game code or data is distributed.

*Kamen Rider: Battride War Genesis* © Bandai Namco Entertainment, © Ishimori Productions / Toei.
