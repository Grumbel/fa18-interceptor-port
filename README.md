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

Front end keys: **F11** fullscreen, **Pause** fast-forward, **F12**
captures the mouse (port 1; the game reads it but flies with joystick or
keyboard).

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

- `--hires N`: render the 3D view at N times resolution (HUD and cockpit
  stay low-res).
- `FA18_SCENE_RENDER=<N>`: alternative software path — captures the 3D
  scene spine into a `SceneFrame`, depth-sorts objects by distance, and
  rasterizes the captured prims at N× resolution (mesh-coloured).  The
  original draw path is unchanged; this only observes.  Implies scene
  capture.  `--hires` takes priority if both are set.
- `--analog`: left stick flies analog (dead zone, proportional pitch and
  rudder) instead of the thresholded digital directions.
- **Back** (keyboard: Insert) opens the debug menu: 3D resolution, analog
  stick, save/load state slots and a way to send ESC to the game.  Save
  states are stored next to the flight log; they can also be loaded from
  the intro.

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
