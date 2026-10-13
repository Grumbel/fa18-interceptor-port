# FA-18 Interceptor Port

## Goal
Reverse engineer the Amiga game F/A-18 Interceptor (1988, Electronic Arts / Intellisoft)
and port it to modern C++ with SDL2 for Linux (and other platforms).

## Source Material
- Original ADF from TOSEC: `FA-18 Interceptor (1988)(Electronic Arts).adf` (not in git)
- Game files extracted with `tools/adf.py` into `assets/`:
  `F-18 Interceptor` (hunk executable, 331232 bytes, 185 hunks, no overlays),
  `config`, `pix/{splsh,frnt5,inst5}` (ILBM), `text/*` (encoded game texts/data)
- NOTE: the assets committed before Oct 2026 were a corrupt extraction
  (truncated executable, wrong config/text files). Docs written against it
  use file offsets that are still valid for the first 311832 bytes.

## Known Facts
- Developer: Bob Dinnerman (Intellisoft), music/sound Dave Warhol
- Hunk 0 (game flow, UI) and hunk 64 are compiled C (Lattice-style startup,
  `link a6` frames); hunks 4–77 are mostly hand-written 68000 assembly
  (3D engine, blitter primitives in hunk 73, Paula sound in hunks 74–77);
  hunks 80+ are C runtime and amiga.lib stubs (with symbols)
- All OS access goes through ~55 library stubs (exec, dos, graphics,
  intuition) plus keyboard/timer/gameport/input/audio devices
- Direct hardware access: blitter (line + fill), Paula, JOY0DAT/JOY1DAT/POTGOR
- Codewheel protection: bypassed exactly like JOTD's WHDLoad slave
  (`docs/whdload/F-18InterceptorHD.s`); WHDLoad PatchSeg offsets are offsets
  into the concatenated hunk payloads (`Machine::patch_address`)

## Architecture
- `src/m68k/` — 68000 interpreter (`cpu.cpp`) + ALU semantics (`ops.hpp`)
  shared with recompiled code. Validated with the SingleStepTests 68000
  suite: `build/cpu_sst <dir>/v1/*.json.bin`
  (https://github.com/SingleStepTests/m68000; only STOP differs, by the
  suite's prefetch-PC convention; address errors are not emulated)
- `src/amiga/` — machine model: NTSC scanline scheduler, copper + bitplane
  display, blitter (area/fill/line), Paula, ports; `hle.cpp` emulates the OS
  calls (line-A traps in fake library jump tables)
- `src/main.cpp` — SDL2 front end (video, audio, keyboard → raw keys,
  mouse, joystick/numpad, `--script` for automated runs)
- Hunks are loaded back to back at `LOAD_BASE` = $20000 (8-byte LoadSeg
  header per hunk); `tools/loadseg.py` and `tools/disasm.py` use the same
  layout, so disassembly addresses equal runtime addresses

## Tools
- `tools/adf.py list|extract` — OFS ADF reader
- `tools/hunks.py` — hunk table; `tools/loadseg.py` — relocated flat image
- `tools/disasm.py` — annotated disassembly (needs m68k objdump:
  `nix build nixpkgs#pkgsCross.m68k.buildPackages.binutils`)
- Automated runs: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/fa18
  --fast --script s.txt --frames N`; script lines are
  `<frame> key <name|hex rawkey> [down|up|tap]`, `joy udlrf`, `mouse dx dy`,
  `shot file.ppm`, `quit` (frame may be `+N` relative)

## Emulator (reference)
- FS-UAE is available for comparing behaviour (`console_debugger = 1`,
  F12+D). Older docs refer to its Slow RAM ($00C00000) addresses; see
  docs/live_symbols.md.

## Workflow
- See TODO.md for current status and next steps.
- Commit directly to the git repository (small, descriptive commits).

## Build

```
nix develop
cmake -B build && cmake --build build
./build/fa18            # looks for ./assets/F-18 Interceptor
```

Or plain CMake with system SDL2.

## License
GPLv3+ for new code. Original game is abandonware but respect IP; this is an
RE / educational port. Game data is not redistributable.
