# TODO – FA-18 Interceptor Port

## Status (Oct 2026)

The game is complete and playable: the original 68000 code is statically
recompiled to C++ (`tools/recomp.py`, generated at build time) and runs on
the runtime in `src/amiga/` (OS high-level emulation + custom chips).

Verified with scripted runs (`--script`):
- title, credits, enlistment/welcome, mission selection
- demo flight, free flight (all airfields, both aircraft), training demo,
  practice, qualification, next active/selectable missions, flight log
- flight log saving (`df0:config`, MODE_OLDFILE write → save dir)
- keyboard flight, throttle, views, weapons keys; joystick via controller
- sound (Paula DMA + audio interrupts; music and effects)
- recompiled build bit-identical to the interpreter (frames + audio) on 19
  scenarios; only the delay() host trap is interpreted
- CPU core passes the SingleStepTests 68000 suite (except STOP's prefetch
  PC convention; address errors not emulated)

## Known gaps / ideas

- [ ] Selectable Missions screen: explore mission selection UI and add
      coverage runs for every mission (only partially exercised)
- [ ] Re-run coverage scenarios after gameplay changes and extend
      `tools/recomp_seeds.txt` (`--coverage` in `--interpret` mode;
      `--misses` in recompiled mode lists anything still interpreted)
- [ ] Sprites are not emulated (game frees the pointer sprite; no other
      sprite use seen so far)
- [ ] CIA timers/serial not emulated (not used by the game)
- [ ] Optional: crossfeed for the hard-panned Amiga stereo, CRT-ish scaling
- [ ] Optional: >15 fps would need patching the game's timer.device based
      frame limiter (and checking the simulation's time-step clamp)
- [ ] Optional: load directly from the ADF instead of extracted files
- [ ] Readable decompilation: the C parts (hunk 0, hunk 64, C runtime) can
      be hand-decompiled into C++ and swapped in function by function (the
      dispatch table maps addresses to functions, so hand-written
      replacements could be registered the same way); the asm parts (3D
      engine hunks 4–77) are better kept as generated code

## Findings worth knowing

- Hunk layout: back to back from $20000; WHDLoad PatchSeg offsets are
  offsets into the concatenated hunk payloads (`Machine::patch_address`)
- Codewheel: flag $5aba4 (patch offset $3a964), check routine in hunk 20
  (wrong-answer store at offset $19737), flag clear NOP'd at offset $1a78
- delay(n) at $208e2: C busy loop used for timed screens → host trap 2
- Display: one View/ViewPort, two copper lists (5-plane and 4-plane
  BitMap sharing planes 0–3) swapped every frame via LOFCprList/DspIns;
  palette fades call LoadRGB4 once per list
- Frame limiter: game polls timer.device TR_GETSYSTIME and caps at 15 fps;
  below ~10 fps the simulation runs slower than real time (stock A500)
- Keyboard: keyboard.device CMD_READ, one event per game frame; Shift is
  sticky (set by $60/$61, consumed by the next key)
- Joystick: JOY1DAT directions + gameport.device unit 1 GPD_READEVENT for
  the fire button; mouse JOY0DAT is accumulated in the VBL server ($292e6)
- The LMB test at $292b4 reads a BSS variable (`_ciaa` linked as data, an
  original bug) instead of CIA-A
