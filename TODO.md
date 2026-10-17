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
- [x] $38baa: set CCR from final moveq (callers branch without tst) — end_*_ok@5550
- [x] projection body: set CCR on moveq #0/#1 and move.w d0,d0 paths
- [x] verify leftovers: $432b8 setw d2-d5; $4136a D(2)=bits (moveq);
      $36fb8 setw d0 (move.w high-word preserve)
- [x] $4120a: move.b/ori.b paths preserve d2 high words; moveq paths zero long
- [x] $438a0/$438a8 re-enabled via trampoline to original $438b6 body
      (native project_screen_body still wrong for positive-d7 and disc paths)
- [x] Scene capture API (phase 1): `scene::SceneFrame` + camera/object
      capture in `$2fc14` (`FA18_SCENE_CAPTURE` / `FA18_SCENE_DUMP`)
- [x] Scene capture phase 2: sorted `list_entry`; `prims[]` from draw hooks
      (poly/line/point) without requiring `--hires`; `render_object` marks
      matching objects; PC hooks install when capture env is set
- [x] Scene capture: mesh_id; real distance from render frame; ISceneRenderer
      - mesh_id = type_index ($5acac / $36094 key); mesh_ptr = $5ad2a after LOD
      - distance = $5ae38 (hypot) at object dispatch; confirmed at render select
      - ISceneRenderer + set_renderer(); called from end_frame()
- [x] Scene capture: prim → object link (`Prim::object_index`, `ObjectDraw::prim_count`)
- [x] Software ISceneRenderer: depth-sort by distance, rasterize prims
      (`FA18_SCENE_RENDER=<scale>`, presents via frame_hires)
- [x] Software hi-res renderer from SceneFrame (all prims, depth-sorted)
- [x] Non-mesh fills captured as Fill2d; drawn by software path (not hidden)
- [x] Prim::rgba from live plane-ops + palette at capture
- [x] Prim-level depth sort (avg view-z); scan-out recording for SCENE_RENDER
- [x] Present: full rendered view + HUD rows only from low-res (no 3D composite)
- [x] Prim verts 32; horizon always captured; SCENE_RENDER installs hooks alone
- [x] Thin-poly stroke; copy-plane rgba approx from \$5ac4c
- [x] Point size kind + disc radius captured and rendered
- [x] Horizon/water: sky_rgba + ground_rgba on SceneFrame; clear to sky
- [x] Horizon: distinct water palette pick; sky prim inject; sky under ground
- [x] Horizon two-pass (fills then meshes); water palette bias toward blue
- [x] Prim xyz capacity 40; dump sel/rgba; note HUD-in-view gap
- [x] Horizon: hook \$44f46 (actual fill path); non-black sky/water fallback
- [x] Horizon: sample sky/water/hy from low-res frame (blitter path, not \$60684)
- [ ] remaining hunk-0 C helpers
- [ ] project_screen_body: fix positive-d7 ($438a0) and disc ($438a8) paths still reached via call_c/sub_XXXXX
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

## Session notes (2026-10-17)

- ADF verified: sha256 3dd48f8985561be22d902393de3baee002d64255fa3826835fcb209875c62540 matches flake requireFile.
- Build succeeds with -DFA18_ADF=... (recomp + native).
- project_screen_body ($438b6 family):
  - Current: $438a0 / $438a8 trampoline to original via call_c(0x438b6).
  - Body implementation in render.cpp is present and *passes* --native verify (0 mismatches)
    on free1 (with --det-timing) and the project_* stats on menu1.
  - Enabling the body (project_438a0/a8 call project_screen_body) keeps free1
    ram-hash bit-identical through frame 3400, but *diverges* on menu1 from ~frame 2900
    (ram-hash mismatch at 2900, 2950, … and at 7000). So the body is still not fully
    equivalent under the menu1 call patterns (comment in source is accurate).
  - Next: isolate the first diverging call on menu1 (FA18_CALLTRACE / FA18_REGS around
    0x438a0 / 0x438b6, or FA18_NATIVE_ONLY=project_438a0,project_438a8 with a short
    --frames around 2900) and compare leftover registers / CCR / a6 frame after the
    projection. Likely a leftover (d2/d3/d7 high words) or a marker-routine side
    effect that only the menu path exercises.

### project_screen_body cycle timing (follow-up)

FA18_VERIFY_ALL=1 + FA18_NATIVE_ONLY=project_438a0,project_438a8 on menu1
(to frame 3100) reports 0 mismatches for every call. free1 remains bit-
identical with the body enabled. Therefore the pure computational result
(registers + memory) of the native body is correct.

The remaining menu1 ram-hash divergence is caused by **emulated cycle
count**: invoke_native does not advance `cpu.cycles` by the cost of the
original instructions, so the NTSC scanline scheduler / copper / IRQs
shift relative to game code. free1 is insensitive; menu1 (many more
projection calls in the menu loop) is not.

Fix options (pick one):
1. Keep the trampoline to `$438b6` (current, zero risk).
2. Measure average cycles of the original body (path-dependent: early
   fail vs full project + marker jsr) and add that to `cpu.cycles` at
   the end of `project_screen_body` (or in a wrapper). Rough estimate
   ~400–800 cycles; needs calibration against a --native off run that
   records entry/exit cycle deltas.
3. Teach `Machine::invoke_native` a per-Def cycle cost (or auto-profile
   the first N calls of each native and then inject the average).

Until (2) or (3) is done the trampoline stays.

### Cycle-cost measurement attempt (2026-10-17 cont.)

Tried fixed `cpu().cycles += 420` (and smaller on early-fail paths) inside
`project_screen_body`. free1 stayed identical; menu1 still diverged at the
same frame 2900. A single average is insufficient because path length varies
(early frustum fail vs full project + marker) and the interaction with the
scanline scheduler is sensitive.

FA18_REGS under --interpret shows many entries to $438b6 but pairing with
the rts sites ($4394e / $43976) did not yield clean per-call deltas in the
logging (interpreter / address mapping quirks).

**Recommended next step (architectural):**
Give `native::Def` an optional `cycle_cost` (or auto-profile the first N
calls of each native under --native verify / a special mode, then inject
the measured average on subsequent calls). That would let us drop the
trampoline for $438a0/$438a8 and improve timing fidelity for every other
native as well. Until then the trampoline stays.

### Scene capture (2026-10-17)

Completed the three open scene-capture fields without touching original
timing or game memory:

1. **distance** — captured from `$5ae38` (hypot `$308cc`) at object
   dispatch in `capture_object`. Confirmed again at LOD selection inside
   `render_body` / `distant_body` via `note_render_object`.
2. **mesh_id** — equals `type_index` (type-table key). `mesh_ptr` holds the
   LOD-selected record at `$5ad2a` once the `$31c8c` family commits to a
   drawable; early-exit paths leave `has_render=false` and `mesh_ptr=0`.
3. **ISceneRenderer** — optional consumer installed with `set_renderer()`.
   Invoked from `end_frame()` after the frame is marked complete.
   `FA18_SCENE_DUMP` stderr output is independent of the renderer.

HD/hires continues to use its own PC hooks; it can later switch to
consuming `SceneFrame` through `ISceneRenderer` if desired. Original
code path and cycle counts are unchanged.

### Scene capture prim→object link (2026-10-17)

`note_object` sets the current object cursor; subsequent `note_prim` calls
stamp `Prim::object_index` and increment `ObjectDraw::prim_count`.  Enables
an ISceneRenderer to group geometry by mesh/distance without parsing draw
order.  Still observation-only.

### Software scene renderer (2026-10-17)

Concrete `ISceneRenderer` in `scene_renderer.cpp`:

- Enable: `FA18_SCENE_RENDER=<scale>` (1..8); implies capture.
- On each completed `SceneFrame`: depth-sort objects by `distance`
  (far → near, painter's algorithm), rasterize attributed prims
  (poly / line / point) into an RGB buffer coloured by `mesh_id`.
- Presentation: when active and a frame exists, `Machine` presents via
  `frame_hires` (same host path as `--hires`).  `--hires` still takes
  priority if both are set.
- Stats: `FA18_SCENE_RENDER_STATS=1` logs object/prim counts per frame.
- Still observation-only; original cycles and memory are untouched.

Next: richer colours from live plane ops, optional HUD composite, or
an OpenGL `ISceneRenderer` using the same `SceneFrame`.

### Software scene renderer HUD composite (2026-10-17)

`compose_software` now nearest-neighbour upsamples the machine's low-res
RGB frame, then overlays only the pixels covered by depth-sorted scene
prims.  HUD, cockpit, and menus stay from the original; the 3D view is
the software re-render.  Same composition model as `--hires`, driven from
`SceneFrame` instead of live PC-hook rasterization.

### Non-mesh graphics in scene capture (2026-10-17)

Horizon / sky / ground / sea are screen-space plane fills (`$44b00`,
`$44b50`), not meshes.  They are now recorded as `PrimKind::Fill2d` with
`object_index = -1` (and `sel = 1` for horizon-class).  SceneFrame opens
at `game_frame` so fills drawn before `$2fc14` are kept.

Software renderer **skips** Fill2d when compositing (default): those
pixels come from the upsampled low-res frame, matching the original
look.  Mesh prims are depth-sorted and overlaid.  `FA18_SCENE_RENDER_FILLS=1`
draws Fill2d in the software path for debugging.

`on_line` also records into SceneFrame (was missing).

### Live prim colours (2026-10-17)

`Prim::rgba` is stamped at capture time from the live plane-ops
(`$5a9de`/`$5a9e0`/`$5ac4c`) plus COLOR00..31.  Software renderer prefers
`rgba` over the mesh_id hash.  Copy-plane ops (`$45014`) leave rgba=0
(depends on destination pixel).  Horizon uses COLOR0 as an approximation.

Also fully restored Fill2d / horizon / on_line capture into SceneFrame
(previous commit only landed the game_frame begin_frame hook).

### Prim-level depth sort + scan-out (2026-10-17)

Software renderer sorts mesh prims by average view-space z (far→near),
falling back to object distance.  Scan-out index/palette recording is also
enabled under FA18_SCENE_RENDER so future composite improvements can use
the same row keys as --hires.

### Coverage dilation in software compose (2026-10-17)

`compose_software` dilates mesh coverage by one low-res pixel and fills the
dilated rim from the nearest covered source.  Reduces low-res mesh flashing
through gaps between hi-res triangles while leaving true non-mesh areas
(horizon, HUD) on the upsampled original.

### Proper software hi-res renderer (2026-10-17)

Stopped compositing the original low-res 3D under the overlay.  The
software path now:

1. Clears the 3D view (COLOR0).
2. Draws **all** captured prims (Fill2d horizon/sky/ground, Poly3d, Line,
   Point) depth-sorted far→near with live rgba when available.
3. Presents that buffer as the entire view rectangle.
4. Takes only rows **below** the view from the low-res frame (HUD/cockpit).

Gaps in capture are visible.  Coverage dilation / mesh_id-only overlay
were removed.

### Capture completeness for software renderer (2026-10-17)

- Prim vertex capacity raised 16 → 32 (original polys can have up to 40).
- Horizon Fill2d always captured when geometry is valid (not only when
  `$5ab93` indicates a second colour); sel distinguishes ground vs sky-class.
- `ensure_software_renderer` installs remaster PC hooks if the machine is
  already live so FA18_SCENE_RENDER works without `--hires`.

### Software renderer raster quality (2026-10-17)

- Thin polys (cables/struts, bbox < 1.5 low-res px) get a stroke outline
  matching the --hires path, not a zero-area fill.
- Copy-plane ops ($45014) no longer leave Prim::rgba = 0; approximate with
  $5ac4c → COLOR so the software path has a real tint instead of mesh_id hash.

### Point/disc capture for software renderer (2026-10-17)

Point prims now store the **resolved** size kind at capture time (0/1/2 or
disc -4 with radius in xyz[1][0]), because -40(a6) limit is only valid during
the original draw.  Software renderer draws 1x1 / 2x1 / 2x2 / disc to match.

### Horizon / water / sky (2026-10-17)

SceneFrame carries `sky_rgba` and `ground_rgba` from the live COLOR
registers (COLOR0 for sky; index 2/3/1 for ground/sea).  Horizon Fill2d
prims stamp matching rgba and update those env colours via
`note_env_colours`.  Software renderer clears the 3D view to sky, then
paints ground/sea fills (sel=1) and other prims on top.  Missing horizon
geometry stays sky — not a low-res underlay.

### Horizon/water draw order and colour pick (2026-10-17)

- Ground/sea colour: pick the palette index among {2,3,6,7,4,5} farthest
  from COLOR0 so water is not identical to sky.
- Fill2d depth: sky (sel=2) behind ground/sea (sel=1).
- When ground is captured, inject a full-view sky Fill2d once per frame so
  the sky is an explicit prim for the software (and future GL) path.

### Horizon two-pass + water palette bias (2026-10-17)

Software renderer draws all Fill2d (sky then ground by depth) before any
mesh/line/point.  Ground/sea palette search covers COLOR1–15 and prefers
entries that are both distinct from sky and relatively blue (water-like).

### Prim capacity 40 + dump detail (2026-10-17)

Original polys can have up to 40 verts; Prim.xyz expanded 32→40.  Scene
dump prints sel and rgba on prims for horizon/water debugging.

Known gap: HUD elements drawn into the 3D view rectangle (not below it)
are not in SceneFrame yet — they will be missing until those draws are
captured as prims.  Not papered over by low-res composite.

### Horizon fill was never captured (2026-10-17)

Root cause: `$44994` BSRs `$44f46` with d0=8 directly and never enters
`$44b00`.  Hook was only on `$44b00` → fill=0 always, view cleared to
COLOR0 (black).

Fix: hook `$44f46` when d0=8,d3=0; capture verts from `$60684`.  If
COLOR0 is black, fall back to mid-blue sky / teal water so the view is
not a void.

### Horizon is a blitter clear, not $60684 verts (2026-10-17)

`$44994` → `$44f46` is a blitter plane-clear driven by edge state, not
the `$60684` polygon buffer (that is for mesh `$44b50` fills).  Capturing
`$60684` at `$44f46` was wrong — either empty or stale mesh verts — so the
view was a flat fallback blue.

Software renderer now **samples the original low-res frame** for sky
(top band), water (bottom band), and horizon row (largest centre-column
colour jump).  Colours match lowres; meshes still drawn on top from
SceneFrame.  Observation only — original draw path unchanged.

### Tilted horizon from low-res (2026-10-17)

Sample horizon row in 11 columns, least-squares fit y = y0 + slope*(x-160).
Paint water below that line so roll tilt matches the original frame.

### Stable flat horizon (2026-10-17)

Tilted least-squares fit was dominated by mesh edges (carrier/aircraft
colour jumps).  Reverted to a flat split: sky/water from top/bottom
*corners*, horizon row = median of a few central columns with outlier
rejection.  Stable and matches lowres colours without chaos.

### Stopped faking horizon from frame_rgb (2026-10-17)

frame_rgb sampling produced a stuck grey band (hy=94, sky=444477) that
tracked neither tilt nor low-res colours — likely wrong-time buffer /
mesh noise.  Horizon remains uncaptured (blitter path).  Software path
clears to neutral dark and draws meshes only.  Also: hook only $38c0c
(was double-capturing every poly via $38c0a fall-through); near-black
prim rgba falls back to mesh_id colour so geometry is visible.

### 3D horizon from view matrix (2026-10-17)

Capture `$5af18` 3x3 view matrix on the camera.  Reconstruct sea/sky as
the plane through world-up in view space: N = M*(0,1,0), horizon where
N·dir = 0, projected with the same 160/90 perspective as meshes.  Proper
3D backdrop from camera pose — not blitter imitation or frame_rgb sample.

### Horizon matrix fix (2026-10-17)

`$5af18` looked HUD-like (flat, instrument colours).  Switch to `$5aed0`
(main 3D rot_out matrix), fall back to player body matrix at +$92.
Fixed sky/ocean colours (not red palette).  Auto-pick Y-up vs Z-up
column.  Stats dump full M[] for tuning.

### Horizon continuous N·d (2026-10-17)

Bank flip was Ny-sign flip at 90°.  Per-pixel N·dir < 0 test is continuous.
Fixed Y-up = column 1 only (no mid-bank axis switch).  Same 160/90
perspective as meshes.

### Horizon column-2 fix (2026-10-17)

User reported roll looked plausible but horizon still wrong; suspected
viewport or instrument ADI vs external sea/sky.

Investigation:
- Software path already restricts itself to the 3D view rectangle
  (rows = $5ac7c+1 from top); HUD/cockpit stay low-res.  Not a viewport
  offset issue.
- Instrument ADI lives in the lower HUD area; the matrix-driven plane is
  only painted inside the 3D view, so it cannot be the instrument.
- Logged $5aed0 under free1 (level then gentle bank):
    level   M=[168 0 0 / 0 0 252 / 0 -128 0]
    banked  M=[166 0 20 / -31 8 249 / -1 -128 4] …
  Column 1 = (0,0,-128) in level flight → N points along view -Z, horizon
  at infinity / full fill.  Column 2 = (0,252,0) → N=(0,1,0), horizon
  correctly at screen y≈90.
- project_screen_body uses focal 160/90 and centre +160/+90; switched the
  synthetic projection constants to match exactly (was 159/89).

Fix: extract world-up from column 2 (m[2],m[5],m[8]), with the matching
row as fallback; keep temporal hemisphere lock.  Comments in
scene_frame.{hpp,cpp} and scene_renderer.cpp updated.

### Horizon column-2 retry with correct side (2026-10-17)

Previous column-2 attempt was reported "much worse".  Likely the plane
test or first-frame sign put water on top.

- First-frame sign is now forced so that the centre-bottom pixel is water
  (N·dir_bottom < 0), independent of Ny sign.
- Temporal lock kept.  Projection centre/focal set to exact 160/90.
- Player-body matrix at +$92 is a clean rotation (scale 16384) with
  column 1 = (0,1,0), but it did not change under free1 stick input in
  the observed frames; $5aed0 does change, so it remains the source.

If still wrong, next step is to abandon pure-matrix horizon and either
(1) capture the real blitter edge state, or (2) fit pitch from a low-res
centre-column colour jump while taking bank from the matrix.
