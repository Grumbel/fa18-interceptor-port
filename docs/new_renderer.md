# New renderer (`--renderer new`): shadow machine re-render

Status 2026-11-11: scene source works as a spike, nothing is drawn with it
yet. Uncommitted; `tests/check_native.sh` has not been run on this tree.

## Why not the state_scene decode

`state_scene` drew `mesh_viewer` decodes with a depth buffer. That loses
what the original's draw stream encodes:

- the stream (`$32666`) is a decision tree of plane nodes; each leaf is the
  back-to-front order of polygon lists for that viewing region, and only
  one side is followed (the decoder follows both);
- a polygon list is abandoned at a back-facing polygon without a back
  colour (deck markings follow their deck), polygons have back colours;
- about 70 op handlers are in use, many reading run-time state (frame
  counter, altitude, scenario record, view matrix);
- everything outside `$2fc14` is missing: the sun (`$43306`, orange disc),
  the ground dots (`$3c1bc`), the effect slots (`$272e4`), and the two
  ground-layer passes over the object list (`$2fa6c`, `$2fa7e`; named
  `start_text_stream_*` in flow.cpp, which is wrong).

This is the cause of the wrong carrier deck and the missing effects.

## Design

`src/render/shadow.{hpp,cpp}`: a second `amiga::Machine` booted from the
same disk. Per game frame the game's RAM is copied at the entry of `$4217c`
(`Machine::capture_image`), the camera is read at `$43306`. Per presented
frame `Shadow::build(alpha, widen, frame)` restores the image, runs
`$4217c`, `$2f4a6`, `$39a54`, `$2f7b8`, `$39b06`, `$39dd0`, `$43306`,
`$2fa6c`, `$2fa7e`, `$3c1bc`, `$272e4` / `$2fc14` (order as in game_frame)
with the leaf rasterisers stubbed (`Machine::add_stub`: `$44b50`, `$44686`,
`$441fc`, `$44212`, `$44276`, `$43dcc`; `$38c0c` runs, its result steers
the model code) and records the primitives (`display_list.hpp`).

- Camera: exact rotation from the view angles (float versions of `$42bbe`
  / `$42b6e`), slerp between two game frames, written back rounded to
  `$5aed0`; camera position lerped into `$5ad5a..`, `$5ad6a..`, `$5ad70`,
  `$5ad74..`. `Frame::refine` = exact matrix x inverse of the quantised one
  removes the matrix's fixed-point error (`$5aed0` has only ~8 bits).
- Widescreen / VR field of view: row 0 of the matrix divided by `widen`.
- Planned renderer: painter's order, no depth buffer (no near plane, so
  nothing disappears close up); the stencil buffer holds the palette index,
  plane ops are stencil REPLACE / INVERT with write masks, resolved through
  the palette in 16 passes. Ground: backdrop index, water polygons from
  `terrain.hpp`, sky as the upper hemisphere of directions (w = 0).

`FA18_SHADOW_VERIFY=1 fa18 --renderer original ...` compares the shadow's
primitives at alpha 1 with what the game drew (`FA18_SHADOW_DUMP=1` lists
the first mismatches). qual_carrier: equal for most frames; open
mismatches: two extra primitives in the game on the first frame (2230) and
in runs later on (from frame ~4022) - not yet looked at.

## To do, in order

1. Finish the verification (all `tests/scenarios`), then interpolate the
   scenario records (+20/24/28 positions, +128/+146 matrices), the small
   records and effect slots between the two images.
2. `src/render/gl_scene`: GLES 2 renderer as above; presentation loop in
   main.cpp on every vsync with alpha from the two snapshot frames; remove
   `state_scene*` and the `strict_renderer` paths.
3. Cockpit and HUD: run the game_frame cockpit block on the shadow too and
   take its 2D layer (two runs on complementary view-area fills give exact
   per-plane transparency), later native / 3D for VR.
4. Snap captured vertices to model vertices to remove the remaining
   one-unit vertex wobble.
