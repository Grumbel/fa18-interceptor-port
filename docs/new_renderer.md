# New renderer (`--renderer new`)

OpenGL 2.1 / OpenGL ES 2 at the window's resolution and the display's frame
rate, widescreen, with the game itself unchanged (same RAM hashes as
`--renderer original`).  Code: `src/render/`, host side in `src/main.cpp`
(`present_new`).  Test: `tests/check_new_renderer.sh ORIGINAL.adf`.

## Why the game's own code draws the scene

The earlier attempt (`state_scene`, removed) drew decoded models with a
depth buffer.  That cannot be faithful:

- A model's draw stream (`$32666`) is a decision tree of plane nodes; each
  leaf is the back-to-front order of polygon lists for that viewing region.
  Only one side of a node is followed.
- A polygon list is abandoned at a back-facing polygon without a back
  colour (deck markings follow their deck); polygons have back colours.
- About 70 op handlers are in use, many reading run-time state (frame
  counter, altitude, scenario record, view matrix).
- Much is drawn outside the object pass `$2fc14`: the sun (`$43306`, a
  disc), the ground dots (`$3c1bc`), the effect slots (`$272e4`), and two
  ground-layer passes over the object list (`$2fa6c`, `$2fa7e`; named
  `start_text_stream_*` in flow.cpp, which is wrong).

So the scene is produced by the original code, run on a copy.

## Shadow machine (`shadow.{hpp,cpp}`)

A second `amiga::Machine`, booted from the same disk, no recompiled code,
no emulated time.  Per game frame the game's RAM and registers are copied
at the entry of `$4217c` (`Machine::capture_image`, about 0.8 MB) and the
camera is read at `$43306`.  `Shadow::build(alpha, widen, frame)` then, for
every presented picture:

1. restores the newest image and writes the moving state of a moment
   between the previous (alpha 0) and the newest (alpha 1) frame into it:
   scenario records (position +20/24/28 with sector +6/+8, angles
   +102..106, matrices +128/+146), small records, effect slots;
2. runs `$4217c`, then replaces `$5aed0` by the exact, interpolated camera
   rotation (float versions of `$42bbe` / `$42b6e`, slerp), row-scaled and
   rounded like the original's; row 0 is divided by `widen` so the
   original's own culling covers a wide picture;
3. runs `$2f4a6` and replaces the camera position (`$5ad5a..`, `$5ad6a..`,
   `$5ad70`, `$5ad74..`) by the interpolated one;
4. runs `$39a54`, `$2f7b8`, `$39b06`, `$39dd0`, `$42f48`, `$3f284` (the
   last two only for the plane masks they leave), then with capture on
   `$43306`, `$2fa6c`, `$2fa7e`, `$3c1bc`, `$272e4` / `$2fc14` as in
   `game_frame`;
5. runs the cockpit / HUD block of `game_frame` twice with the real
   drawing routines, on a view area of all zeros and of all ones: pixels
   that come out equal were drawn (overlay), the others belong to the 3D
   view.

Capture: PC hooks at `$38c0c` (polygon), `$43a56` / `$4397c` (line),
`$4389c..$438b4` + the plot leaves (point, disc), `$44b50` (2D fill).
During 4 the leaves are stubbed (`Machine::add_stub`): `$44b50`, `$44686`,
`$441fc`, `$44212`, `$44276`, `$43dcc`.  `$38c0c` itself runs: its result
steers the model code.  Plots made inside the fill and line routines
(`$44dfe`: polygons of one or two pixels become dots or lines) are not
primitives; the renderer applies the same minimum size.

Precision: `$5aed0` has about 8 bits, so `Frame::refine` (exact matrix x
inverse of the quantised one) takes the captured view-space words to the
exact camera.  A captured vertex that is the transform of a vertex of the
model being drawn (`glr::set_object_observer`, pool `$5d684`, checked
against the captured words) is replaced by its unrounded transform; see
`Capture::snap`.  Derived vertices (runway markings, extrusions), shadows
and effects keep the original's one-unit rounding.

`FA18_SHADOW_VERIFY=1 fa18 --renderer original ...` compares the shadow at
alpha 1 with what the game drew in the same frame (`FA18_SHADOW_DUMP=1`
lists mismatches).  Under 1% of frames differ, at view changes (one-shot
flags consumed before `$4217c`).

## Renderer (`gl_scene.{hpp,cpp}`)

- Painter's order as captured, no depth buffer, clip z constant: no near
  or far plane, nothing disappears close up.
- The stencil buffer holds the palette index (bit n = bitplane n).  A
  plane operation is stencil REPLACE under a write mask, an inversion
  INVERT, the plane copy of `$45014` two masked passes.  Sixteen passes
  resolve the indices through the palette of the game's screen (row 20 of
  the scan-out).  With a multisampled window this is antialiased.
- Ground: stencil cleared to the backdrop (4 land, 6 open sea), the water
  polygons of `terrain.hpp` at sea level (plane 1), the sky as the upper
  hemisphere of directions (w = 0; plane 0).
- Projection: columns `159 - 160 x/z`, rows `89 - 90 y/z` (points one row
  lower) of 320x200, i.e. clip `x = -x/z - 1/(320 widen)`,
  `y = 0.105 + 0.9 y/z`.
- Lines: 0.6 low-res pixels wide, at least 1.25 output pixels.
- Overlay (cockpit, HUD, messages) and non-flight screens: textures with
  sharp enlarged pixels, per-row palette of the game's screen.

## Presentation

`present_new` draws once per emulated frame and then, until the next one
is due, further pictures of the moments in between (vsync paces the loop;
without vsync `--render-fps`, default 60).  The picture lags one game
frame, so every moment shown lies between two frames the game computed.
`FA18_PRESENT_DEBUG=1` reports pictures per second and the time per
picture; `FA18_SHADOW_PROFILE=N` the shadow's stages.

Environment switches: `FA18_NO_INTERPOLATION`, `FA18_SHADOW_ALPHA=x`
(tests), `FA18_NO_SNAP`, `FA18_MSAA=n`, `FA18_NO_VSYNC`, `FA18_NEW_4X3`.

## Open

- Pause and everything outside the 3D view is the game's 320x200 screen.
- Cockpit and HUD are the original's pixels (drawn per picture, so they
  move smoothly); a vector HUD and the 3D cockpit for VR come next.
- Derived vertices are not snapped (see above).
- `$42f48` / `$3f284` run on the shadow only for their plane masks; about
  a third of the shadow's time.
- Not yet tried on a real display by the author: vsync pacing at 120/144
  Hz, GLES 2 devices, the web build (single canvas).
- VR: `widen` and the camera override are the hooks for a head pose; the
  display list is renderer-independent (`display_list.hpp`).
