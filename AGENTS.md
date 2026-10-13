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
- Only one agent works in this repository at a time (or use separate git
  worktrees); uncommitted changes of another agent leak into your builds.

## Native C++ translation rules (src/native/)
The goal is readable C++ that is *exactly* equivalent to the original
routines. Every commit touching src/native/ must pass the gate:

    tests/check_native.sh            # must print PASS

It runs all scenarios with `--native on` and `--native off` in
deterministic timing and compares memory hashes every 50 frames, then runs
`--native verify` (per-call comparison). Never commit on a failure; never
commit with a routine disabled "for now" without saying so in TODO.md.

- Translate from the assembly (`tools/disasm.py`); Ghidra pseudo-C
  (tools/ghidra) is only a reading aid — it drops code after calls it
  thinks never return and cannot read Lattice switch tables.
- Exact 68000 semantics: `bset/bclr/bchg/btst` on memory are byte
  operations (bit n of the byte at the address = high byte of a word
  variable); `cmp`/branch signedness (bcs/bhi/bls unsigned, blt/bgt signed);
  `move.w` into d0 keeps the upper word; Lattice passes shorts as 32-bit
  stack slots.
- Translate the whole routine. No placeholders like "rest stays original
  for now" unless the remaining part is called as original code.
- Conventions: compiled C routines (`link a6`) use `NATIVE_C` with typed
  parameters; invoke_native reproduces their link/movem frame and saved
  registers, and `call_c` leaves registers as the callee left them, so
  leftover-register and caller-frame tricks of the original keep working.
  Assembly routines that change registers their callers can see must use
  `NATIVE_ASM` and set those registers (see stick_v/joy_fire_down).
  Mark a routine `verify = true` only if it has no OS side effects.
- Name game variables in globals.hpp (`Var<T>`); `vars.hpp` has raw
  size-typed accessors. A `Var` converts to its *value*; use `.ptr()` for
  its address and `.get()` when assigning one Var to another (`a = b` is a
  compile error: it would copy the address, not the value).
- Debugging a failure: `tests/ramdiff.sh SCENARIO FRAME DIR` (first
  differing bytes), `FA18_NATIVE_ONLY=name1,...` / `FA18_NATIVE_SKIP=...`
  to bisect routines, `FA18_CALLTRACE=from,to,file` to diff the order of
  routine entries, `FA18_POISON` to find routines reading registers they
  were not given.

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
