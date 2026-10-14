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

The original game files, i.e. the contents of the game disk, in `assets/`
(`F-18 Interceptor`, `config`, `pix/`, `text/`). They can be extracted from
an ADF with

```bash
python3 tools/adf.py extract "FA-18 Interceptor (1988)(Electronic Arts).adf" /tmp/fa18
cp -r "/tmp/fa18/F-18 Interceptor" /tmp/fa18/{config,pix,text} assets/
```

The recompiled code is generated from `assets/F-18 Interceptor` during the
build (needs Python 3); without it (`-DFA18_RECOMP=OFF`) the interpreter is
used.

## Build

```bash
nix develop          # or: install cmake, SDL2, python3
cmake -B build
cmake --build build
./build/fa18
```

or `nix build && ./result/bin/fa18`.

## Running

```
fa18 [options] [data-dir]
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
