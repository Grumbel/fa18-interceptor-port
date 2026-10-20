# OpenGL renderer (`--gl N`)

`./build/fa18 --gl N <adf>` renders the 3D view with OpenGL at N times
the resolution (N = 1..8), with a depth buffer and a real ground plane.
Code: `src/native/gl_renderer.{hpp,cpp}`; host side `init_gl()` in
`src/main.cpp`.  Without a GL context (e.g. `SDL_VIDEODRIVER=dummy`) it
falls back to the software hi-res renderer (`--hires N`); headless tests
work with `SDL_VIDEODRIVER=offscreen`.

## Pipeline

The original renderer keeps running unchanged (game state is identical
with and without `--gl`).  Per game frame:

1. `$2112c` (game frame): start a capture.
2. Every object the original draws announces itself when its LOD is
   chosen (`render_body` `$31e4a`, `distant_body` `$32dda` ->
   `glr::begin_object`): flat models (mesh header +7 bit 1, not
   articulated) and the distant ground objects go to the ground layer.
3. The primitives the original rasterises are captured in view space
   (the hires.cpp hooks: polygons `$38c0c`, lines `$43a56`/`$4397c`,
   points `$4389c`..`$438b4`), together with their plane operation.
   Each object vertex stage has its own fixed-point unit; scaled back to
   world units the primitives of all objects share one depth buffer:

   | Stage | World units per view unit |
   |-------|---------------------------|
   | `render_body` (`$31d6c`) | `2^($5adb0 + 8 - -6(a6))`, times `2^$61284` for polygons |
   | `distant_body` (`$32d7e`) | `2^(8 + -8(a6))` |

   Primitives before the first object (the terrain of `$3f284`, the sky
   fill) are not captured: the ground is rebuilt in world space instead.
4. At the end of the scene (`remaster::scene_end`) the frame is drawn
   into an off-screen framebuffer of palette indices:
   - clear to index 5 (sky = plane 0 on the land backdrop);
   - land plane at Y = 0 (index 4) and the water polygons of the terrain
     tables (plane 1 set; `$5aaa8`: colour 6), triangulated once at
     start-up (`terrain.cpp`, far grid outside the map, near grid inside),
     in world space relative to the camera `(cell($5ac44) << 22 - $5ad5a,
     $5ad5e, cell($5ac46) << 22 - $5ad62)`; see `ground_sea_sky.md`;
   - flat objects in the original's order without depth test (runways,
     islands, markings: no z-fighting);
   - 3D objects with the depth test; inside an object later primitives
     get a small depth bias so coplanar details (decals) stay on top.
   Plane operations (set / clear / invert planes) are GL logic ops
   (`AND ~clr`, `OR set`, `XOR xr`) on the index byte.  The projection
   is the original's (`sx = 159.5 - 160 vx/vz`, `sy = 89.5 - 90 vy/vz`);
   depth is logarithmic in vz (the world spans 2^29 units).
5. The indices are read back into the hi-res layer of `hires.cpp`, whose
   composition is unchanged: pixels the game drew after the scene (HUD,
   cockpit) and 2D drawing during the scene that no 3D primitive explains
   come from the low-res frame, everything else from the layer, through
   the live per-row palette (fades, flashes).

## Why primitives and not decoded models

The plan was to draw each object from the offline model decoder
(`mesh_viewer.cpp`) with a per-object transform captured at the object's
dispatch.  Two findings from free1 (on the runway, frame 5000) rule that
out as the exact solution:

- Polygon ops reposition sub-parts at run time: the runway object
  (mesh `$4c06a`) draws its runways with the object position
  `$5ae28` = (917504, 0, 3276800) and then, after an op, its markings with
  (1001472, 0, 2985984).  The decoder, which runs the ops without the
  live state, returns only the four runway polygons.
- The vertex stages of one object use different fixed-point units
  (header block: `-6(a6)` = 1, `-8(a6)` = 1; later ranges in `$328ea`:
  4 and 0), so a transform captured once per object does not describe all
  of its vertices.

Capturing the original's primitives keeps everything the ops do (sub-part
placement, animation `$335f8`, the damage/state LOD lists of `$369cc`,
`$36a26`, `$36ac6`) without re-implementing them, and the depth buffer and
world-space ground still remove the painter's-algorithm and horizon
artefacts.  The original rasteriser (`$38c0c` onwards, the blitter code)
still runs because its input is what is captured; skipping it would need
the polygon ops translated to emit primitives directly.

## Open points

- The plane copy operation (`$45014`, display plane 3 -> 2 inside the
  polygon) cannot be expressed as a logic op; such primitives are skipped.
- GLES 2 has neither logic ops nor `gl_FragDepth` (needs
  `EXT_frag_depth`); a GLES port needs a fallback (colour on the land
  backdrop, standard depth).
- Primitive precision is the original's 16-bit view space; models are not
  smoother than in the original, only rasterised at a higher resolution.
- Discs (`$438a8`) are drawn as polygons in the view plane, points as
  square GL points.
