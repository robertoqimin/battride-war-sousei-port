# battride-war-sousei-port

A native PC (Linux) port of **Kamen Rider: Battride War Genesis** (仮面ライダー バトライド・ウォー 創生, PS4, **CUSA02716**, version 01.02).

It works like Wine + DXVK, but built for a single game:
- the **game's original x86-64 code runs directly on the PC's CPU**;
- a runtime written for this game replaces the PS4 system libraries;
- the PS4's graphics (GCN/PM4) are translated to **Vulkan**.

The project is a fork of [bbport](https://github.com/deadinside28/bloodborne_pc), the native Bloodborne port, which itself grew out of [shadPS4](https://github.com/shadps4-emu/shadPS4). Full credits are in [CREDITS.md](CREDITS.md).

> **No game files are included.** You need a **dump of your own copy**: the base game CUSA02716 v01.00 and the v01.02 update, taken from your own console.
> This project is not affiliated with Bandai Namco, Toei or Sony Interactive Entertainment.

---

## Status

**Experimental, playable.** Tested on an AMD RX 6750 XT (Mesa/RADV, Linux).

| Area | Status |
|---|---|
| Boot, menus, movies, saves | ✅ |
| Story Mode, tutorial, combat | ✅ (validated through the first missions) |
| **60 FPS gameplay** | ✅ default (the original caps at 30); game speed verified |
| Smoothness | ✅ 100% of combat frames at 16.7 ms on the benchmark route |
| Audio (BGM, effects, voices; ATRAC9 via Ngs2) | ✅ no reverb, envelope or mastering yet |
| Gamepad (DualShock, DualSense, Xbox via SDL) | ✅ |
| Keyboard and mouse with the game's own layout | ✅ |
| Online (leaderboards etc.) | ❌ reports "signed out" |
| Windows / Steam Deck | untested |

Step-by-step development reports (in Portuguese) are in [`docs/relatorios/`](docs/relatorios/). The screenshots they mention are **not published**, because they are game content.

---

## Building

### 1. Dependencies (Linux)
You need:
- GCC or Clang, CMake, Ninja and pkg-config;
- the **Vulkan** headers and loader, **SDL3** and zlib;
- Python 3.

See `port/build.sh`. On NixOS, bbport's `shell.nix` is a good reference.

### 2. Compile
```sh
cd port
./build.sh            # produces port/out/bb-probe and port/out/gpu/libbbgpu.so
```

### 3. Prepare your game
The dump must be **decrypted**: a plaintext ELF/SELF `eboot.bin`, which is what modern dumpers produce.

```sh
# merged base + update view (symbolic links only; nothing is copied or modified)
tools/make_game_view.sh /path/to/CUSA02716 /path/to/CUSA02716-patch game

# analyze and link the game's executable and modules (produces out/boot-linked.bin)
python3 port/scripts/prepare.py      game --out out
python3 port/scripts/link_libc.py    game --out out
python3 port/scripts/link_modules.py game --out out
```

### 4. Play
```sh
cd port
./out/bb-probe ../out/boot-linked.bin --app0 ../game --user ../user --timeout 0
```
Saves are stored in `user/savedata/`.

---

## Options (environment variables)

| Variable | Effect |
|---|---|
| `KR_FPS=30` | restores the original 30 FPS gameplay cap (default **60**) |
| `KR_RES=1440p` | render size: `720p`, `1080p` (the game's own), `1440p`, `4k` or `WIDTHxHEIGHT`. Above your window's size it works as supersampling (sharper edges). RX 6750 XT: 1440p holds 60 FPS, 4K drops to 43-55 FPS in combat |
| `KR_DOWNSCALE=0` | turns off the area filter used when the frame is larger than the window (default **on**: each window pixel averages the frame pixels it covers, in linear light; off: the old bilinear read) |
| `KR_NGS2_GAIN=x` | overall audio gain |
| `BB_CONFIG=file.ini` | remap controls (`key.<input>=Keys`, `pad.<input>=buttons`; see `port/src/runtime_pad/bindings.c`) |
| `BB_FPS_PRINT=1` | prints the frame rate to the terminal every 5 s |

Diagnostics: `KR_TRACE_NGS2`, `KR_NGS2_PROF`, `KR_IO_TRACE`, `KR_STALL_PROFILE`, `KR_PAD_TRACE` and `BB_FRAME_LOG=<csv>`.

---

## Controls

This is a Japanese release: **○ confirms and ✕ goes back.**

| Action | PS4 pad | Keyboard and mouse |
|---|---|---|
| Move / camera | sticks | WASD / mouse (or numpad 8-4-5-6) |
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

---

## Benchmark

```sh
tools/bench/bench.sh my_run [VAR=value ...]   # fixed automated route → out/bench/*.json
```

---

## Layout

```
port/      runtime + GPU layer (fork of bbport; GPL-2.0-or-later)
tools/     scripts: game view, imports, benchmark and automated routes (MIT)
ghidra/    scripts to recreate the executable analysis from YOUR dump (MIT)
docs/      game map, roadmap and per-step reports, in Portuguese (MIT)
```

## License

- Files **outside `port/`** are under the **MIT** license ([LICENSE](LICENSE)).
- The **`port/`** directory stays under **GPL-2.0-or-later** ([port/LICENSE](port/LICENSE)), like the bbport and shadPS4 code it comes from. Some parts carry other licenses, stated in their own files.
- The compiled program (`bb-probe`) is distributed under the GPL.

Kamen Rider: Battride War Genesis © Bandai Namco Entertainment, © Ishimori Productions / Toei.
