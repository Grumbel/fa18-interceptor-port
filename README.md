# FA-18 Interceptor – Modern Port

Reverse-engineering and port of the classic 1988 Amiga flight simulator
**F/A-18 Interceptor** (Electronic Arts / Intellisoft) to C++ / SDL2.

## Build (Nix)

```bash
nix develop          # enter dev shell with SDL2 + cmake
cmake -B build
cmake --build build
./build/fa18
```

Or build the package:

```bash
nix build
./result/bin/fa18
```

## Build (plain CMake)

Requires SDL2 development packages:

```bash
cmake -B build
cmake --build build
./build/fa18
```

## Controls

| Key | Action |
|-----|--------|
| SPACE / ENTER | Advance high-level state (title → flight, …) |
| BACKSPACE | Previous state |
| ESC | Quit |
| UP / W | Pitch up (hold) |
| DOWN / S | Pitch down (hold) |
| LEFT / A | Yaw left (hold) |
| RIGHT / D | Yaw right (hold) |

Axes return to centre on release (stand-in for JOY0DAT stick).

## What’s working

- High-level state machine (Startup → Title → … → Flight)
- ILBM loader for `frnt5` / `inst5` (row-interleaved planar, ByteRun1)
- Flight view: sky/ground split, runway wireframe, horizon, HUD crosshair
- Attitude filter + integrate driven by held keys
- Object pool with `$c6`-style motion step and palette-coloured wireframes
- Recovered Amiga 16-colour palette + copper/display constants in code

## Recovered (docs/, not fully ported)

- Display: View/ViewPort, ColorMap, live copper list, register-script
- Audio: 4 Paula channels, sample-block queue from action table
- 3D: Q2.14 matrix, 7200-circle angles, perspective `(x·160)/z+160`
- Input: `$155` keyboard path, JOY0DAT accumulators
- Objects: 64-byte stride pool, dual-tier velocity, damping flags

## Status

See `TODO.md` for tip bundle and open items. Remaining attitude/draw
work needs live FS-UAE dumps (recipes in TODO).

## License

New code: GPLv3+.
Original game assets remain under their original copyright; this is an
educational / clean-room style reverse-engineering effort.
