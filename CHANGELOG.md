# Changelog

## 0.1.0 — 2026-11-19

The first release: a native Linux port of Kamen Rider: Battride War Genesis (PS4, CUSA02716 v01.02).
It runs from your own dump of the game and its update; no game files are included.

### Works
- Boot, menus, movies, saves, Story Mode and combat (validated through the first missions).
- 60 FPS by default (the original caps at 30), with the game speed unchanged.
- Audio: music, effects and voices (ATRAC9 through Ngs2).
- Gamepads through SDL (DualShock, DualSense, Xbox), and keyboard and mouse with the game's own layout.
- The game's C++ exceptions, through its own libc unwinder.
- A desktop launcher (GTK 4 + libadwaita) that checks your dump, prepares the game and sets the
  window, render size, frame rate, graphics options and controls; also packaged as an AppImage.

### Graphics options
- Render size from 720p to 4K or any size (`KR_RES`); above the window's size the frame is
  area-filtered down (supersampling).
- Frame rates from 30 to 240 or the screen's refresh rate (`KR_FPS`, `BB_VBLANK_HZ`), VSync on or off
  (`BB_PRESENT_MODE`).
- Anti-aliasing of the 3D scene before the HUD is drawn: SMAA or FXAA (`anti_aliasing=` in `BB_CONFIG`).
- FSR 1 upscaling for a render size below the window's (`KR_UPSCALER=fsr1`).

### Known issues
- Not yet played through: the final boss, the ending and credits, Time Survival, Free Mission and
  the galleries.
- Online features report "signed out".
- No reverb, envelopes or mastering in the audio yet.
- A crash in the sound thread was seen once and has not been reproduced.
- No DLSS or FSR 2/3; Windows and the Steam Deck are untested.
- Some code and names still come from bbport (the `bb-probe` executable, `BB_*` variables).
