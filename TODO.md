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
- [x] Load directly from the ADF instead of extracted files (runtime Adf reader in src/amiga/adf.hpp; directory mode kept as fallback)
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
recompiled original. Gate: `tests/check_native.sh` must PASS (see the
rules in AGENTS.md).

Done (all verified by the gate):
- startup: main, picture loader, delay, Workbench close, splash reuse
- flight log / config, codewheel hash
- hunk-0 game state machine: all handlers in tools/extra_entries.txt,
  game_frame ($2112c), run_game_state, poll_input, keyboard/joystick/
  gameport/mouse input helpers, fades, messages, world-origin/display
  resets (see the NATIVE_* registrations in flow.cpp); the handlers were
  reviewed line by line against the patched asm
- control-stick/surface deflections ($25584), per-frame sound update
  ($24aa8), aim angles ($24552), cockpit command/view toggles
  ($23d54/$241f0/$2439a/$243fa), engine/airspeed update ($25edc with
  $269ce/$269fa/$26a3a; engine.cpp, scenario eng_poke), altitude/
  ground-contact update ($26b16 with $27290; vertical.cpp, scenario
  ground_poke), effect slots ($272e4/$27554/$277e0/$27b06/$27c2c;
  effects.cpp, scenario effects_poke; $27a30 is never called), gun burst
  sound ($2a2f8, sound_api.cpp)
- UI text formatting helper $397a2: append one hexadecimal digit as ASCII
  to the output buffer (flow.cpp, NATIVE_ASM; equivalence gate pending).
- asm math helpers (math.cpp, all NATIVE_ASM, verified per call): hypot
  $308cc, isqrt $39bba, vector scaling $39cb6/$39cc0/$39d58, matrix rotate
  $39de2, rounded divide $39eec, BCD <-> binary $39f2e/$39f74, view pan
  $39e34, view sequence $39b06, command flags $39c70, list reset $39dd0.
- per-frame scene update asm (flight.cpp): $39a54, $2f4a6, $2f74e,
  $2f596, $2f9da, $2f7b8, $3127c (with $313fa), stubs $3a310/$3a312.
  $2f596 and $2f7b8 are not verified per call ($36b88 uses a beam-position
  random number / many callees); equivalence run covers them.
- 3D scene list builder (scene.cpp): $30064 with its internal subroutines
  ($3034c/$3043c/$30478/$30530/$3067a and the tail $30b5c-$3127a) and the
  projection step $30872 (falls through into the $308cc hypot).
- visibility classification of the object list (visibility.cpp): $31494
  with helpers $31b04-$31c28.
- 3D object transform / polygon-op driver (render.cpp): entries $31c8c/
  $31c9c/$31d64 and the distant variant $32d64/$32d72 with their
  subroutines ($3223e, $328ea, $32adc/$32aec, $32b92, $32f6e), polygon
  ops $32ffa/$33052/$3309e, $33120/$33140, $31c50. Registered internal helpers $3223e/$32adc/$32aec/$32b92/$31c28/$35832,
  matrix project $3f7e2 for
  original callers. Native $4389c family (screen projection, shared by both).
  Still original: larger bulk $33fda/$40136 and some
  op-adjacent code; op handlers at $33478 are in ops.cpp.
- world-origin vectors ($2d3cc/$2d3e0/$2d3f4/$2d408/$2d41a) and object
  matrix xform ($2d452/$2d468/$2d48e/$2d4a0/$2d4a6),
  player-object reset $2d880 / flight-setup $2d8e0, stick-lo bits
  $2e22c/$2e230/$2e234/$2e23a, $2f098, $399ee, $397a2 as NATIVE_ASM in flow.cpp
- scene draw top (scenedraw.cpp): camera position $2f220 and the object
  draw loop $2fc14 with helpers $2fffc/$3000e. Op handlers behind
  the jump table at $33478 are in ops.cpp up to $35f48 (all handlers
  of the $33478 tables done; read-verified only, no scenario hits them: $335f8, $33de6, $35730, $357ee,
  $33e74, $34708, $34754); then HUD / object AI.
  Condition codes are only set where the original's last instruction
  defines them (not compared by --native verify).

Done (batch): $399f6 airspeed limits, $397b2 name compare,
  $38baa player-ready predicate, $4120a object steer flags.
  $388ee obj params, $3ab16/$3b462 persp correct, $42d8c matrix row scale.
  $3cfc6/$432b8/$4136a/$36fb8 screen/object helpers.
  $44160/$4419e/$3000e/$42682..$42696 buffer/object setup.
  $42eba sincos, $2f11a/$2f124 player flag, $31b34 cam delta.

Open:
- [x] Fix $38baa player-ready predicate (non-mode-3 path always failed; must
      test object+110 after bit2 check, same as mode-3 tail)
- [x] Fix $42eba sincos: Q2/Q4 leftover d6/d7, and table index must
      sign-extend like 68000 (0,a0,dn.w) (was zero-extending uint16_t)
- [x] Fix $36fb8 clear-lock: bit6-clear must *clear* selection (was inverted)
- [x] Fix $4120a object steer flags: non-0x60 path (bit7-clear and altitude
      bands); mode 0x60 keeps 0x80/0x40 when alt skips ori (ble/bgt go to
      merge $41286, NOT moveq #0); d3 leftover = altitude on that path
- [x] Fix $399f6 airspeed limits: divu.w overflow; blt skips body but still
      copies $5adea→$5adf2 / $5adee→$5adf6; body path ALWAYS writes $5adde
      (9999 path bras to that store, max-skip ble also falls into it)
- [x] Fix $4389c positive-d7 path: write shifted threshold back to d2
- [ ] $438a8 native disabled (free1 DIFF@3300); body math independent of
      d7 yet only the -4 entry diverges; $35832 kept as word-save version
      that matched skip_all; long movem rewrite regressed
- [x] $31b34 cam delta: word subtract before swap/asr must preserve sign
      (uint zero-extend of a negative word difference was wrong)
- [x] $4120a: set D(2)=bits as long (moveq-style) not setb only
- [x] $3cfc6 lookup_scaled_byte: table displacement is 18 not 12
      (opcode 1030 1012 brief ext disp=$12); caused menu5/qual_carrier@2250
- [x] $3ab16/$3b462: divs.w overflow leaves destination unchanged (effects_poke@5050)
- [ ] remaining hunk-0 C helpers
- [ ] $438a8 still disabled (disc projection path)
- [ ] later-frame diffs: end_*_ok@5550, mission6@6500, menu1@6100, aero_all@8800 still reached via call_c/sub_XXXXX
      (audio channel set-up, view set-up $27f0c/$27d50, menus/text
      $393f6/$396e2, flight-log screens, ...). $39510 (mission-specific
      message/value lookup into the queue) has been translated in flow.cpp;
      its native equivalence gate still needs to be run in an SDL2 build.
- [ ] C runtime library parts that are executed (hunks 80+)
- [ ] assembly subsystems: hunks 4-40 (3D engine, flight model, HUD,
      objects; ~22000 executed instructions), blitter primitives (73),
      sound driver (74-77). Backlog measured with --interpret --coverage:
      422 executed non-native functions, ~26000 distinct instructions
      (236 of <=30 insns, 125 of 31-100, 61 larger; the biggest are the
      3D object renderer $33fda, $3223e, $33160, $40136, $31494, $31c50).
      Math helpers $308cc/$39b06-$39f74 are done; next: the flight model
      and 3D projection path. Counts include embedded data tables.
      `$201b8` (startup tail) is intentionally left original.
      Most are pure memory/register routines: use
      NATIVE_ASM and --native verify routine by routine.
- [ ] extend tests/scenarios when translating code the scenarios do not
      reach (check with --coverage in --interpret mode)
