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
- [ ] Optional: on-screen pie-menu overlay for the gamepad layers
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

## Native C++ translation (in progress)

Hand-translated routines live in src/native/ and replace the original code
by address (`--native on`, default); `--native off` runs only the
recompiled original. Equivalence oracle:
`tests/compare_native.sh DIR` (deterministic timing `--det-timing`, memory
hash every 50 frames, all scenarios) must report "same" for all.
Debug aids: tests/ramdiff.sh, FA18_NATIVE_SKIP / FA18_NATIVE_ONLY,
FA18_POISON, `--native verify` (per-call comparison for pure routines).

Done: the entire hunk-0 game state machine from extra_entries.txt,
  plus non-state helpers:
  $2151c/$215fe/$21630/$216c2/$21750/$21bfc/$2346a/$234a8/
  $23c24/$23c66/$23c9c/$23d08/$25e8c/$28d4a/$28dae/$28e5c/
  $29006/$29074/$292b4/$29c60/$2f024/$2f02c.
  All NATIVE_STATE handlers are registered; see the list in flow.cpp.
Open:
- [ ] game_frame ($2112c) is translated but not registered: enabling it
      gives a 1-byte divergence ($5ab55) around frame 2250 of free1.
      Investigation: $5ab55 is owned by the object system at $310c0+;
      condition audit of game_frame found no mismatch.  Likely a
      not-yet-native callee.  $23c9c is now native.
- [ ] remaining hunk-0 C helpers still called via call_c (input, audio
      channel, view setup, etc.), C runtime, asm hunks 4-40 (3D, flight
      model, HUD), sound driver (hunks 74-77)
Workflow: tools/ghidra/DecompileAll.java gives pseudo-C (reading aid only;
Ghidra drops code and mis-types switches - translate from the assembly).
Next tip work: more hunk-0 helpers (input $29xxx, audio $29c60), or
bisect $5ab55 with FA18_NATIVE_SKIP.
