# State-driven renderer (work in progress)

Goal: render the 3D view and cockpit from the game's *state* (camera,
aircraft, world objects, flight data) instead of capturing the 68000's
rasteriser/blitter output.  See `gl_renderer.md` for the capture-based path,
which stays as the oracle.

## Step 1: `GameState` snapshot (`src/native/game_state.{hpp,cpp}`)

`gamestate::read(reader)` snapshots camera, the 16 scenario records, the 16
small records and the 10 effect slots straight from RAM.
`fa18 --state-dump FRAME PATH ADF` writes it as text at an emulated frame
(`Machine::frame_hook`); `tests/game_state.cpp` covers the layout.

Verified on `tests/scenarios/free1.txt`, frames 4000 and 4655:

- Attitude words (`+102` pitch, `+104` heading, `+106` bank) use **28800
  units per turn**, not 7200.  Heading 2240 = 28.0 deg matches the body
  matrix at `+146` (`14466, -7692` / 16384 -> atan = 28.0 deg); pitch 28008
  = -9.9 deg matches `sin = 2817/16384`.
- `body_m` (+146) is a Q14 orientation basis; the camera `view_m` ($5aed0)
  is the same rotation scaled per row (148/252/113 vs 14466/16384/14466).
- Unused scenario slots hold flags `0x0010`; the player is slot 0 (index
  from `$5abd6`); slot 14 in free flight (flags `0x405c`, no attitude) is a
  static scenario object.  Effect slots and the 32-byte records are idle in
  plain flight (records read `00 10 00 00 fe a3 ...`, i.e. free).
- Static scenery (buildings, runways, islands, lights) is **not** in these
  pools.  It comes from the world database (`world.hpp`: three grid maps of
  `{type, subcell, position}`), already decodable with
  `world::decode_exe` / `world::decode`.

## Next steps

1. Model library: reuse `mesh_viewer::load_models` (object-space polygons,
   lines, points and plane ops per LOD) as the mesh source.  Open: ops that
   depend on run-time state (runway markings, damage, articulated parts).
2. Scene from state: scenario objects + world-map objects around the camera
   cell + terrain (`terrain.cpp`, `ground_sea_sky.md`), float transforms with
   the camera from `GameState`, depth buffer; flag `--state-scene`.
3. Diff against the capture path on the same replay frame.
4. Cockpit instruments as functions of `GameState` (pitch/bank globe, radar,
   readouts) replacing the blit-capture paths.

## Vertex-stage scaling (findings from render.cpp `render_body`, $31ede / `s328ea`)

- Per-object vertex shift is **distance dependent**: frame local -8 = `$5adb0` + stage shift,
  local -6 = fraction bits. The stage shift is chosen by comparing the camera-relative
  position components (after `asr` by the LOD shift) against the threshold in local -150
  (see the `L31ec4`/`L31eda`/`L31ede` ladder): near objects keep full precision,
  farther ones drop 1-2 bits.
- Vertices: `v >> local(-8)` -> rotate by `$5aed0` (`muls`, `asr 8`) -> add the
  camera-relative offset (`$5ae28 + camera`, `>> local(-6)`/8 shift).
- Consequence: a state-driven generator must reproduce the shift ladder exactly
  (or use float transforms and accept sub-unit differences). Per-object attribution
  of captured prims via `object_index` is ambiguous, so validate by comparing
  whole-frame prim sets, not by per-object ratio fits.
- Next: implement `gamestate -> view-space prims` with float math (model scale =
  2^(shift) world units per model unit), compare against `SceneFrame` for frames 4000/4655.

## Validated model->view formula (frame 3966, free1 replay, type 86 tower)

    view = view_m * ( v/8 + (obj_eye - cam_eye) ) / 256        (view_m is row-major $5aed0, Q8)

where `v` is the mesh_viewer object-space vertex, `obj_eye = pos >> 8` and `cam_eye` the
player's `pos >> 8`. Predicted (1273,-7,1330) vs captured (1272,-13,1319): x within 1 unit;
the y/z residue (6/11 units) is eye truncation (`pos >> 8`) - use full-precision
pos/cam differences (`(pos_obj - pos_cam) / 256` as float) in the real renderer.
The "/8" is the model scale for this model (header nibble / LOD shift); verify for
other models (type 74 etc.) before generalising.

## TODO: g-force redout/blackout
The game dims/reddens the view under g-load. Locate the g-load/vision state in RAM, add it to GameState and render it in the state scene.

## Status: static objects from state (frames 3966 / 4000)

`state_scene::build_static` (src/native/state_scene_build.cpp) places near-map objects from
`GameState` + `world::decode_exe` + `mesh_viewer` LOD0 prims using the validated formula.
With `FA18_STATE_CALIB=<frame>` the probe finds every captured object polygon (the type 86
tower) among the generated ones (tolerance 12 view units); the remaining captured polygons are
`object_index == -1` ground/terrain fills, which the existing terrain code covers. Types 42/74
are generated but the game skips them (distance/clip culling not yet modelled).
Next: distance/LOD selection + culling, scenario objects (body_m rotation), far maps.

## LOD selection and culling (frames 3966, 4000, 4655)

- Object distance = hypot(pos_obj - pos_cam) / 4096 (world units), matches `$5ae38` (tower 207, type 74 123).
- LOD limit = distance * `$5ad3a` / 128 (camera zoom, now `Camera::zoom`). The LOD with the largest
  decoded distance <= limit is used; `Model::cull_distance` (first empty decode) means "not drawn".
- Primitives entirely outside one frustum plane of the game's projection are dropped.
- Result: generated object set equals the captured set in all three frames (tower present at
  3966/4000, none at 4655; types 42/74 culled). `$5adb0` shift is still ignored (0 in free flight).

## Scenario objects and per-model scale (mission7_combat, frame 5715)

- The camera for relative positions is `Camera::eye` ($5ad74), not the player's position
  (seat offset); using it removed the 6/11-unit residue seen on the tower.
- Model scale: mesh header byte +6, low nibble n: 1 eye unit (256 world units) = 2^n model
  units (tower n=3, aircraft n=6). The game's per-object vertex-stage unit varies with n and
  distance, so captured view coordinates differ from ours by a constant per-object factor
  (1 for the tower, 64 for the aircraft); projection only uses ratios, so it cancels.
- Scenario objects: `view = view_m * (body_m * v / 16384 / 2^n + (pos - eye) / 256) / 256`
  with body_m row-major (the transpose is wrong). The model comes from the type-table entry
  `$36094 + 20*slot` matched to decoded models by (draw_fn, base, lod_list).
- Result: all 11 captured polygons of the nearby aircraft (scenario 1) match to ~1 view unit.
  Caveat: scene capture happens before the frame's flight update, so state read at end of
  frame differs by one step of motion; the renderer must read state at the right point (or
  interpolate, see gl_camera_interp.hpp). The frustum cull is switchable
  (`state_scene::set_frustum_cull`) because the game draws polygons outside the view too.
