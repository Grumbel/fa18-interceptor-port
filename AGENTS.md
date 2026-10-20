# FA-18 Interceptor Port

## Goal
Reverse engineer the Amiga game F/A-18 Interceptor (1988, Electronic Arts / Intellisoft)
and port it to modern C++ with SDL2 for Linux (and other platforms).

## Source Material
- Original ADF from TOSEC: `FA-18 Interceptor (1988)(Electronic Arts).adf`
- **The ADF is not part of the repository and must not be committed.** Keep it
  outside the tree (or pass its path at runtime / to CMake). The runtime loads
  assets directly from the ADF — no unpack step is required.
- Disk contents (for reference): `F-18 Interceptor` (hunk executable, 331232
  bytes, 185 hunks, no overlays), `config`, `pix/{splsh,frnt5,inst5}` (ILBM),
  `text/*` (encoded game texts/data). `tools/adf.py list|extract|extract-one`
  can still dump the image if needed.
- NOTE: any `assets/` tree committed before Oct 2026 was a corrupt extraction
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

## Remaster options (off by default; original behaviour untouched)
- Enhancements are runtime options, never part of the translated routines'
  default path, so the gate always runs the original behaviour.
- `--analog` (also in the debug menu): gamepad left stick drives pitch/rudder
  deflection analog (hook in `process_control_input`, controls.cpp);
  scripted runs ignore real pads; script command `analog <x> <y>|off`.
- `--hires N` (N=2..8): the 3D view is re-rendered at N times resolution
  from the original view-space polygons/lines (`src/native/hires.cpp`);
  the original rasteriser keeps running, so game state is identical.
  Observed via pc hooks (`Machine::add_pc_hook`) on $38c0c (polygons),
  $43a56/$4397c (lines), $44b50 (2D fills); compose() uses the hi-res
  layer where the game did not draw after the scene ended (HUD/cockpit
  stay low-res) and falls back to low-res for changes it could not
  explain. `FA18_HIRES_DEBUG=1` paints the fall-back pixels magenta.

- Debug menu: gamepad Back button (keyboard: Insert) pauses and offers
  3D resolution (original/2x/3x/4x), analog stick, save/load state slots
  (`<save dir>/stateN.sav`) and "send ESC". Save states are applied at the
  top of the game's main loop ($27d50), the only point where all state is
  in RAM/registers; script commands `savestate N`, `loadstate N`, `menu`.

## Tools
- `tools/adf.py list|extract` — OFS ADF reader
- `tools/hunks.py` — hunk table; `tools/loadseg.py` — relocated flat image
- `tools/disasm.py` — annotated disassembly (needs m68k objdump:
  `nix build nixpkgs#pkgsCross.m68k.buildPackages.binutils`)
- Automated runs: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/fa18
  --fast --script s.txt --frames N`; script lines are
  `<frame> key <name|hex rawkey> [down|up|tap]`, `joy udlrf`, `mouse dx dy`,
  `shot file.ppm`, `quit` (frame may be `+N` relative)
- Object models: debug menu → Mesh viewer, or `./build/fa18 --mesh-dump DIR
  <adf>` (summary + one PPM per model LOD); format in docs/mesh_format.md

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
`--native verify` (per-call comparison). In a recompiled build it also
checks that `--interpret` gives the same memory as the recompiled code
(`FA18_SKIP_INTERP=1` skips that step). Never commit on a failure; never
commit with a routine disabled "for now" without saying so in TODO.md.

- Translate from the assembly of the *patched* program (`tools/disasm.py`
  and `tools/loadseg.py` apply the codewheel bypass and the delay() trap
  exactly like `Machine::boot`; the bytes in the ADF executable differ, e.g.
  the store of $5aba4 at $21a80 is a NOP). Ghidra pseudo-C
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
  Assembly callees often consume *leftover registers* of the compiled C
  code that calls them (e.g. $2e7e2 ends in the key tail $2ef98 which reads
  the key code from d0). When native C code calls such a routine, set the
  registers as the original code had them (find out with
  `FA18_REGS=addr,count ... --native off`, which logs all registers when
  execution reaches addr, and by comparing FA18_CALLTRACE/FA18_WATCH runs).
- The polygon-op handlers ($32ffa-$35f48) live in `ops_body.inc`, compiled
  by ops.cpp (live machine) and by mesh_ops.cpp (the mesh viewer's offline
  decoder, `mesh_emu.hpp`).  Use only unqualified helpers there (cpu(),
  rd*/wr*, call_c, asmr), no `native::` names or globals.hpp variables.
- Name game variables in globals.hpp (`Var<T>`); `vars.hpp` has raw
  size-typed accessors. A `Var` converts to its *value*; use `.ptr()` for
  its address and `.get()` when assigning one Var to another (`a = b` is a
  compile error: it would copy the address, not the value).
- Debugging a failure: `tests/ramdiff.sh SCENARIO FRAME DIR` (first
  differing bytes), `FA18_NATIVE_ONLY=name1,...` / `FA18_NATIVE_SKIP=...`
  to bisect routines, `FA18_VERIFY_ALL=1` (with `--native verify`: check
  every call, not just the first 200), `FA18_CALLTRACE=from,to,file` to diff the order of
  routine entries, `FA18_POISON` to find routines reading registers they
  were not given.

## Build

```
nix develop
cmake -B build && cmake --build build
./build/fa18 /path/to/FA-18\ Interceptor\ (1988)(Electronic\ Arts).adf
```

Or plain CMake with system SDL2. The ADF is a **runtime** argument only; it is
not needed to configure or compile. Without an original executable at build
time the binary uses the 68000 interpreter. To also enable static recompilation,
optionally pass `-DFA18_EXE=/path/to/F-18 Interceptor` (or `-DFA18_ADF=...` to
extract it once for the recompiler). The ADF is never installed or committed.

### Checks

```
nix flake check
# or only the native gate:
nix build .#checks.x86_64-linux.native
```

`checks.build` always builds the package (interpreter-only without the ADF).

`checks.native` runs `tests/check_native.sh` inside the normal Nix sandbox.
The original ADF is a `pkgs.requireFile` input (not committed). Add it once:

```
cp "FA-18 Interceptor (1988)(Electronic Arts).adf" fa18-interceptor.adf
nix-store --add-fixed sha256 fa18-interceptor.adf
```

(Store path names cannot contain spaces; the content hash is what matters.)
If the file is missing from the store, the build fails with that instruction.


## License
GPLv3+ for new code. Original game is abandonware but respect IP; this is an
RE / educational port. Game data is not redistributable.
