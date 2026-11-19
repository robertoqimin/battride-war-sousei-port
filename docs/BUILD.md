# Building from source

How to build and run the port from this repository on Linux, and how the release AppImage is made.
Players don't need this: the [latest release](https://github.com/robertoqimin/battride-war-sousei-port/releases/latest)
has a ready AppImage (see the [README](../README.md)).

## Steps

### 1. Dependencies (Linux)
You need:
- GCC or Clang, CMake (3.24 or newer), Ninja, pkg-config and git;
- the **Vulkan** headers and loader, and **glslang** (`glslang` or `glslangValidator`);
- **SDL3**, zlib and libX11;
- **FFmpeg** (libavformat, libavcodec, libavutil, libswscale, libswresample);
- **Boost** (headers), **fmt**, **xxHash**, **tsl-robin-map** and **Zydis**;
- the header-only **xbyak** and **Vulkan Memory Allocator** (`vk_mem_alloc.h`);
- Python 3.

magic_enum and miniz are optional: CMake downloads them when the system has none. OpenSSL is optional.

On Arch Linux and CachyOS:
```sh
sudo pacman -S --needed base-devel cmake ninja git pkgconf vulkan-headers vulkan-icd-loader glslang \
    sdl3 zlib libx11 ffmpeg boost fmt xxhash robin-map zydis miniz python
```
Arch has no package for xbyak or VMA. Put their headers in a folder (for example `port/out/deps`, which git
ignores) and point the build at it:
```sh
mkdir -p port/out/deps/vma
git clone --depth 1 --branch v7.24 https://github.com/herumi/xbyak.git port/out/deps/xbyak-src
ln -s xbyak-src/xbyak port/out/deps/xbyak
curl -fsSL -o port/out/deps/vma/vk_mem_alloc.h \
    https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/v3.3.0/include/vk_mem_alloc.h
export CMAKE_INCLUDE_PATH="$PWD/port/out/deps" CPLUS_INCLUDE_PATH="$PWD/port/out/deps"
```
These variables must be set for every `./build.sh` run.

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

### 4. Play with the launcher (recommended)
```sh
tools/launcher/launcher.py              # GTK 4 + libadwaita (Python 3 with PyGObject)
tools/launcher/install-desktop.sh       # optional: adds it to the application menu
```
Choose your base game and update folders and press **Prepare & Play**: the first launch links them
into the data folder and prepares the game (step 3, about a second). The Settings, Keyboard and
Controller pages set the window mode, render resolution, frame rate and controls; the Log page
follows the game's output. In a checkout already set up as in step 3 the launcher uses it as
it is (`game/`, `out/` and your saves in `user/`).

#### The launcher as an AppImage
```sh
tools/appimage/build_appimage.py --tools ~/tools/appimage   # -> out/appimage/BattrideWarGenesis-0.1.0-x86_64.AppImage
```
`--tools` holds `appimagetool-x86_64.AppImage` (1.9.0), `runtime-x86_64` (AppImage type 2) and a static
`patchelf` (0.18.0). The AppImage carries the launcher, the built port and the Python + GTK 4 +
libadwaita they need; no game code or data. It runs on systems whose glibc is at least the build
system's, so one built on a rolling distribution needs a recent one. For the release build, which
runs on Ubuntu 22.04 (glibc 2.35) and newer, build it inside Ubuntu 22.04 with Docker:
```sh
tools/appimage/build_portable.sh --tools ~/tools/appimage   # needs the xbyak/VMA folder from step 1
```
The data folder defaults to `~/.local/share/battride-war-sousei-port`.

### 5. Or play from the terminal
```sh
cd port
./out/bb-probe ../out/boot-linked.bin --app0 ../game --user ../user --timeout 0
```
Saves are stored in `user/savedata/`.

### 6. Release build and corresponding source
```sh
tools/appimage/build_portable.sh --tools ~/tools/appimage --sources
```
Builds the AppImage inside Ubuntu 22.04 and, with `--sources`, `BattrideWarGenesis-<version>-sources.tar`:
the source of every third-party binary in it (`tools/appimage/collect_sources.py`, adapted from
battride-war-recomp's). Both go on the release page with their `.sha256` files.

## Options (environment variables)

| Variable | Effect |
|---|---|
| `KR_FPS=N` | gameplay frame rate, 30 to 240 (default **60**; `30` is the original cap). The game speed stays the same. Above 60, also set `BB_VBLANK_HZ=N` |
| `BB_VBLANK_HZ=N` | the emulated vblank rate (default **60**); flips wait for it, so it must be at least `KR_FPS` |
| `KR_UPSCALER=fsr1` | a render size below the window's (`KR_RES`) is upscaled by AMD FSR 1 (EASU) instead of bilinearly; the HUD is upscaled with the scene |
| `anti_aliasing=` in `BB_CONFIG` | `smaa` or `fxaa`: anti-aliasing of the 3D scene before the HUD is drawn (the HUD stays sharp); `off` (default) |
| `BB_PRESENT_MODE` | `Mailbox` (default: VSync without tearing), `Immediate` (VSync off) or `Fifo` (strict VSync: a late frame halves the rate) |
| `KR_RES=1440p` | render size: `720p`, `1080p` (the game's own), `1440p`, `4k` or `WIDTHxHEIGHT`. Above your window's size it works as supersampling (sharper edges). RX 6750 XT: 1440p holds 60 FPS, 4K drops to 43-55 FPS in combat |
| `KR_DOWNSCALE=0` | turns off the area filter used when the frame is larger than the window (default **on**: each window pixel averages the frame pixels it covers, in linear light; off: the old bilinear read) |
| `KR_AVPLAYER_HOLD=N` | movie frames kept from the decoder after the game moves past them (default **3**; `0` = the old behaviour, which showed a wrong frame for an instant several times per second) |
| `KR_NGS2_GAIN=x` | overall audio gain |
| `BB_CONFIG=file.ini` | remap controls (`key.<input>=Keys`, `pad.<input>=buttons`; see `port/src/runtime_pad/bindings.c`) |
| `BB_FPS_PRINT=1` | prints the frame rate to the terminal every 5 s |

Diagnostics: `KR_TRACE_NGS2`, `KR_NGS2_PROF`, `KR_IO_TRACE`, `KR_STALL_PROFILE`, `KR_GAME_PROFILE=<file>` (with `tools/game_profile.py`), `KR_UNWIND_TEST=<eboot return offset>` (throws a C++ exception there to check unwinding), `KR_PAD_TRACE`, `BB_GPU_PROFILE` (+ `BB_GPU_PROFILE_SLOW_MS`) and `BB_FRAME_LOG=<csv>`.

## Benchmark

```sh
tools/bench/bench.sh my_run [VAR=value ...]   # fixed automated route → out/bench/*.json
```

## Layout

```
port/      runtime + GPU layer (fork of bbport; GPL-2.0-or-later)
tools/     launcher, game view, imports, benchmark and automated routes (GPL-2.0-only)
ghidra/    scripts to recreate the executable analysis from YOUR dump (GPL-2.0-only)
docs/      game map, roadmap and per-step reports, in Portuguese (GPL-2.0-only)
```
