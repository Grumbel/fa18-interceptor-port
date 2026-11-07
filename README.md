# F/A-18 Interceptor – Modern Port

Port of the 1988 Amiga flight simulator **F/A-18 Interceptor**
(Bob Dinnerman / Intellisoft, Electronic Arts) to C++ and SDL2.

The game's original 68000 code is statically recompiled to C++ at build time
and runs on top of a small runtime that provides what the code expects from
an Amiga 500: the handful of AmigaOS calls it makes (exec, dos, graphics,
intuition and the keyboard/timer/gameport/audio devices) and the custom
chips it programs directly (copper/bitplane display, blitter line and fill
drawing, Paula sound). A 68000 interpreter is kept as fallback and as a
reference; both produce bit-identical frames and audio.

The complete game works: title, enlistment, demo flight, free flight,
training, qualification, missions and the flight log (saved to disk).

## Requirements

The original game disk image (ADF), e.g. the TOSEC dump
`FA-18 Interceptor (1988)(Electronic Arts).adf`. The ADF is **not** shipped
with this repository and must not be committed to it — pass its path at
runtime and (for a recompiled build) to CMake.

The runtime opens the ADF directly; there is no need to unpack the disk into
an `assets/` directory. A legacy directory of unpacked files is still
accepted if you already have one.

The ADF is only needed at **runtime** (pass it as the data argument). Build
does not require it: without an original executable CMake builds the
interpreter-only binary. Optional static recompilation needs the executable
once at build time (`-DFA18_EXE=...`, or `-DFA18_ADF=...` to extract it).


## Platforms

Desktop Linux remains the primary target. Additional ports (scaffolded after
[kurvenrausch](https://github.com/Grumbel/kurvenrausch)):

| Package | Command | Notes |
|---------|---------|-------|
| WebAssembly | `nix build .#fa18-wasm` / `nix run .#fa18-wasm` | Upload ADF in the browser (IDBFS); not embedded |
| Windows 64/32 | `nix build .#fa18-win64` | MinGW + official SDL2 MinGW DLLs |
| R36S / ArkOS | `nix build .#fa18-r36s` | PortMaster launcher; place ADF in the port folder |
| Android | `nix build .#fa18-android` | APK; `adb push disk.adf` into app files dir |

The original ADF is never redistributed. See `TODO.md` for open port work.

## Build

```bash
nix develop          # or: install cmake, SDL2, python3
cmake -B build
cmake --build build
./build/fa18 "FA-18 Interceptor (1988)(Electronic Arts).adf"
```

or `nix build && ./result/bin/fa18 path/to/game.adf`.

## Running

```
fa18 [options] [data]
  data           path to the original .adf, or a directory of unpacked
                 disk files (default: looks for a nearby .adf / ./assets)
  --speed X      CPU speed (default 4: the game caps itself at 15 fps;
                 1 = stock A500, 6-8 fps in flight and slower simulation)
  --save DIR     config / flight log directory
                 (default $XDG_DATA_HOME/fa18-interceptor)
  --scale N      window scale
  --protection   keep the codewheel check (default: bypassed like the
                 WHDLoad install)
  --interpret    use the 68000 interpreter instead of recompiled code
```

Developer options: `--fast`, `--frames N`, `--screenshot N file.ppm`,
`--script file`, `--wav file`, `--coverage file`, `--misses file`,
`--verbose`, `--trace` (see `fa18 --help` and AGENTS.md).

## Controls

Keys are passed to the game as on the Amiga (see `manual.txt`):

| Key | Function |
|-----|----------|
| Cursor keys | Fly (up = nose down, down = nose up, left/right = roll) |
| F1–F10, `=` / `-` | Throttle (F10 twice = afterburner) |
| Space / Return | Fire / select weapon |
| `,` `.` | Rudder |
| G, A, Backspace | Gear, arrestor hook, brakes |
| H, J, C, F, M, R, T | HUD, ECM, chaff, flare, map, radar range, target |
| Keypad | View directions; Keypad Enter = outside view |
| `[` `]` | Zoom |
| P, Esc, Shift-Esc | Pause, restart, back to mission selection |

Front end keys: **Ctrl+B** disables original Amiga blitter writes for a
native-graphics comparison; **Ctrl+Shift+B** hides only original bitplane
pixels while preserving guest RAM. **Ctrl+Alt+B** cycles selective original
bitplane masks (instruments, panel, upper view), also preserving guest RAM.
**Ctrl+F9** or **Insert** opens the debug menu, **Ctrl+F10** mesh
viewer, **Ctrl+F11** switches between original 320×200 and the last enhanced
graphics setting (OpenGL/software, including its scale; the first switch from
original defaults to 3× OpenGL, or software if GL is unavailable), **F11**
fullscreen, **Pause** fast-forward, **F12** captures the mouse
(port 1; the game reads it but flies with joystick or keyboard). Plain
**F9/F10** go to the original game for throttle/afterburner.
The debug menu has a two-page control reference (**H** on keyboard, **Y** on
gamepad); Left/Right switches between original Amiga keys and gamepad bindings.

### Game controller

The original Amiga game only supports a **digital** joystick (four
directions + fire on port 2) — it never reads the proportional pot
registers.  The left stick is therefore thresholded to digital
directions.  Remaining functions are mapped to buttons and two hold
layers that work like pie menus (hold the shoulder, push the stick
into a direction: the entry fires immediately; click the stick for the
centre entry):

| Input | Action |
|-------|--------|
| Left stick / D-pad | Fly |
| A | Fire |
| B | Select weapon |
| X | Cycle target |
| Y | Map |
| LT / RT | Throttle down / up |
| Start / Back | Pause / debug menu (see below) |
| LS click / RS click | Brakes / Gear |
| Right stick X / Y | Rudder / Zoom |
| **LB + stick** (push) | Systems pie: N radar, E target, S map, W HUD, NE ECM, SE chaff, SW flare, NW arrestor, stick click = gear |
| **LB + face** | A HUD, B ECM, X chaff, Y flare |
| **RB + stick** (push) | View pie (keypad directions); stick click = forward, **RB+A** = outside view |

### Remaster options and debug menu

All off by default; the original behaviour is untouched.

To run the current native OpenGL scene **and** the direct GPU compositor:

```sh
fa18 --gl 4 --gl-direct --widescreen --render-fps 120 /path/to/original.adf
```

`--gl-direct` alone now selects 3× OpenGL as a convenient default. The
`--hires` software renderer and `--gl` OpenGL renderer are mutually
exclusive; the command prints the active 3D renderer and presentation
backend at startup (`graphics: ...`). The menu's **3D renderer** setting
changes scene rendering, not how the final window is presented. The
**GL output** status at the bottom of Graphics identifies either the
SDL/CPU compositor or direct GPU output. Direct presentation must be
selected at startup because the visible window needs an OpenGL context.

| Renderer setting | 3D view | Presentation |
|---|---|---|
| Original | Amiga 320×200 | SDL |
| `--hires 4` | 4× software rasterizer | SDL |
| `--gl 4` | 4× OpenGL scene | CPU compositing, then SDL |
| `--gl 4 --gl-direct` | same 4× OpenGL scene | direct GPU composition |
| Original selected in Graphics after `--gl-direct` | Amiga 320×200 | GPU bitmap presentation |

- `--hires N`: render the 3D view at N times resolution (HUD and cockpit
  stay low-res).
- `--gl N`: draw the captured 3D scene with OpenGL at N times resolution.
  `--widescreen` extends the world beyond the original 4:3 view (initially
  16:9, adjusts to window resizing), leaving the original scene and HUD
  unchanged in the centre. `--render-fps 120` (also 180 or 240) redraws
  interpolated OpenGL geometry between game frames without increasing the
  game's physics tick rate; HUD and unconverted 2D elements still update at
  their original frequency. Widescreen now extends the scene down beside the
  cockpit; the centre retains the original cockpit artwork. These options
  have no effect on non-GL scenes.
- `--gl-direct` (experimental; also `FA18_GL_DIRECT=1`): present
  OpenGL directly to the game window, compositing palette/scanline colours
  and the original 2D cockpit/HUD layer on the GPU. Avoids expensive GPU
  scene readback and the full-frame SDL texture upload on normal frames;
  stats and the measured `--fps` counter are themselves composited by GL;
  title/menu/mesh-viewer and other non-3D screens now use a native GPU
  bitmap presenter on the same window, rather than an SDL window swap.
  The SDL path remains for screenshots, `--fast` and GL failures. For testing,
  `FA18_GL_DIRECT_COMPARE=1` checks the visible frame's logical pixel
  centres against the CPU compositor (and intentionally reads pixels back).
  It implies `--gl 3` when no renderer was specified; combining it with
  `--hires` is an error. `--widescreen` and `--render-fps` require OpenGL.
  For interactive direct presentation, avoid the testing-only `--fast` mode.
  When a new 3D scene is still being drawn, widescreen holds the preceding
  complete scene instead of exposing the half-written Amiga back buffer.
  See `tests/check_gl_direct.sh`, `tests/check_gl_widescreen_stability.sh`,
  and `docs/gl_renderer_roadmap.md`. The old
  3D rasterizer still executes in the emulated game for now.
- `FA18_SCENE_RENDER=<N>`: software hi-res path from the captured
  `SceneFrame` — depth-sorts all prims (fills, polys, lines, points),
  rasterizes them at N× into the 3D view, then pastes only HUD/cockpit
  rows from the low-res frame.  Incomplete capture shows as missing
  geometry (not papered over by the original low-res 3D).  Observation
  only.  Implies scene capture.  `--hires` takes priority if both are set.
- `--analog`: left stick flies analog (dead zone, proportional pitch and
  horizontal steering) instead of the thresholded digital directions.
  Input is inserted at the original code's aircraft-control record reads
  in native, recompiled and interpreted execution alike.
- `--text-pace`: menu texts, briefings and cockpit messages at the pace of
  a stock A500 (they otherwise run several times faster; the game itself
  is not slowed down, see docs/frame_timing.md).
- `--stats`: show flight/object diagnostics at the actual top-left corner
  of the viewport, including in widescreen. `--fps`: show measured window
  presentation FPS (which can differ from the 120/180/240 Hz target).
- **Back** (keyboard: Insert) opens the debug menu. **Graphics >** contains
  3D renderer and scale (original/software/OpenGL), widescreen, render FPS
  (60/120/180/240, only effective in OpenGL), full detail, stats overlay,
  FPS counter, an **Amiga blitter** on/off switch, and a separate **Amiga
  bitplanes** visibility switch.
  **Ctrl+B** switches it instantly while playing, with a visible red
  `AMIGA BLITTER OFF` label and a window-title warning. The blitter's
  destination writes (including line/area blits) stop, but its registers,
  completion interrupt and cycle accounting continue so gameplay can still
  advance. This is a deliberately destructive *visual debugging* mode:
  source bitmap pixels are hidden, menus or original overlays can
  disappear, and game RAM no longer matches a normal replay. It is **not**
  a completed replacement of the original renderer. Start in this mode
  with `--no-amiga-blitter`; scripted runs accept `blitter off`/`blitter on`.
  `tests/check_amiga_blitter_toggle.sh ORIGINAL.adf` exercises this switch.
  **Ctrl+Shift+B** (or `--no-amiga-scanout`, script `scanout off|on`) hides
  the Amiga display's bitplane pixels *without* suppressing any writes.
  This is reversible and RAM-equivalent to the normal game. The native
  cockpit can therefore be measured separately using
  `tests/check_gl_scanout.sh ORIGINAL.adf` and `tools/scanout_diff.py`.
  For an individual instrument, **Ctrl+Alt+B** cycles a masked Amiga
  source-screen region, leaving all other bitplanes visible. The equivalent
  `--hide-amiga-region globe` (or `left-attitude`, `status`, `fuel`, `radar`,
  `readouts`, `front-cockpit`, `lower-panel`, `upper-screen`, `none`) can be
  changed during replays via `scanout-region globe` or `scanout-region none`.
  `tests/check_gl_scanout_region.sh ORIGINAL.adf globe` compares the
  resulting image and unchanged RAM hashes. Region masks do **not** replace
  any renderer yet; they make missing original-image dependencies easier
  to locate without changing what the game writes.
  **Controls >** contains analog-stick steering and the controller/keyboard
  reference; **Extras >** contains Mesh Viewer and text pacing. The main
  menu retains Resume, Save/Load states, Send ESC and Quit. Press Esc/B to
  leave a submenu without closing the menu; H/Y opens the control reference
  directly from any page. Widescreen and render rate can be changed live.
  **Save states >** lists existing saves to overwrite (requires confirmation),
  plus **New Save** for a fresh, uniquely named file. **Load states >** lists
  all compatible `.sav` files, including the old `stateN.sav` slots. The lists
  scroll without a fixed slot limit (Up/Down, Left/Right or PgUp/PgDn to page,
  Enter/A to select, Esc/B to go back). Entries summarize the captured game
  RAM: mission, map cell, altitude, speed and enemy progress. Save files live
  in `--save DIR` / the normal user-data directory beside the flight log and
  can be loaded even from the intro.

The debug menu and its controls reference use the game's original **5x7
bitmap font**, loaded at runtime from Hunk 61 of the ADF executable. The
font's 53 glyphs are not embedded in the port; when the expected table is
unavailable, the menu falls back to the built-in host font. To extract a
preview sheet (or the raw glyph bytes) from your own ADF:

```sh
python3 tools/extract_menu_font.py game.adf /tmp/fa18-menu-font.ppm
# Optionally: add --raw /tmp/fa18-menu-font.bin
```

## Project layout

- `src/m68k/` – 68000 interpreter and shared ALU semantics
- `src/amiga/` – machine model, custom chips, OS high-level emulation
- `src/recomp/` – interface to the generated code
- `tools/` – ADF/hunk tools, disassembler, recompiler (`recomp.py`)
- `tests/cpu_sst.cpp` – CPU conformance test (SingleStepTests 68000)
- `docs/` – reverse-engineering notes

## License

New code: GPLv3+. The original game remains under its original copyright;
this is an educational reverse-engineering project.
