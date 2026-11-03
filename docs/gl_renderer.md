# OpenGL renderer (`--gl N`)

`./build/fa18 --gl N <adf>` renders the 3D view with OpenGL at N times
the resolution (N = 1..8), with a depth buffer and a real ground plane.
Code: `src/native/gl_renderer.{hpp,cpp}`; host side `init_gl()` in
`src/main.cpp`.  Without a GL context (e.g. `SDL_VIDEODRIVER=dummy`) it
falls back to the software hi-res renderer (`--hires N`); headless tests
work with `SDL_VIDEODRIVER=offscreen`.

## Widescreen and higher presentation rate

`--gl 3 --widescreen` starts at 16:9 and extends the visible 3D world left
and right as the SDL window aspect ratio changes. The vertex shader scales
its X clip coordinate by `320*N / framebuffer_width`: no central stretch or
change of the original camera's horizontal field of view. `compose_view`
uses the same GL scene for the centre and both side bands: the original
Amiga double buffer can lag by one capture and previously produced a
temporally mismatched seam. Only unclassified 2D drawing and cockpit/HUD
foreground pixels are copied from the old framebuffer. At free1 frame 4000,
the 960x600 centre of a wide 1284x600 120 Hz screenshot differed from its
4:3 **same-phase** screenshot at just one polygon raster edge pixel on Mesa.
Menus retain their original logical aspect. The stats overlay now has a
fixed logical destination rectangle and does not stretch across widescreen.

`--render-fps 120` (or 180/240) optionally adds extra OpenGL presentations
between the original 60 Hz emulated VBL ticks. The original game continues
to simulate physics and render its own low-resolution view at its existing
pace; the host re-renders only cached GL primitives, with no additional 68000
execution. Two consecutive 3D captures provide view-space vertex positions
and the camera pose. The game's depth-sorted draw list can change object
order from one capture to the next. Object instances are paired by mesh,
stream and nearest view-space centre, with each old instance used only once;
within each pair, primitive sequences and drawing operations must agree.
These pairings are cached once per capture, not rebuilt every presentation.
Compatible vertices move linearly from the older to the newer capture;
primitives whose LOD, clipping or topology has changed retain their newest
position. Ground and water use the
interpolated camera. The renderer measures the *actual* interval between 3D
captures (often four VBLs, about 15 Hz) to advance the blend smoothly, with
one capture interval of visual latency. Large gaps reset history to prevent
blending across menus or scene cuts.

The HUD, 2D overlays, explosions not captured as geometry, and the original
physics still advance only at their original rates; interpolation applies
only to geometrically matched 3D primitives and camera movement. Additional
redraws are skipped when the host falls too far behind its 60 Hz deadline.
For inspection, `FA18_GL_INTERP_DEBUG=1` reports interpolation and extra
presentation counts, while `FA18_GL_MATCH_DEBUG=1` shows matched primitives,
including those recovered across draw-list reorderings.
`tests/check_gl_scene.sh` asserts the same-phase 4:3 centre is preserved
apart from a handful of edge-raster precision differences, in addition to
interpreter/native/recompiler parity;
`tests/check_gl_interpolation.sh` verifies extra blended presentations,
identical emulated RAM at 60 and 120 Hz, and equal 120 Hz screenshots in
interpreter, recompiled and native execution.
`tests/data/interpolation_motion.txt` is a separate accelerated free-flight
replay for visual inspection: at frame 4655 (241 knots), its 60 and 120 Hz
OpenGL screenshots differ in 3,778 output pixels on the tested offscreen
Mesa renderer, demonstrating actual scene motion rather than just extra
identical presentations.

`FA18_GL_PRESENT_DEBUG=1` reports the **observed** presents per second and
missed extra-frame deadlines during normal-speed 120+ Hz play. This is
the important diagnostic when high refresh seems ineffective: CPU emulation,
GPU/CPU readback, compositing and SDL presentation can leave too little
time for an extra redraw. `FA18_GL_PROFILE=1` reports per-20-capture
primitive/draw-call/vertex counts, draw submission, synchronous readback
and palette conversion time. The hidden GL context still reads pixels back
into SDL; an on-screen GL compositor is the next architecture step (see
`docs/gl_renderer_roadmap.md`).

Full-detail baseline on the bundled free1 replay (3,700 deterministic,
fast-forwarded frames, native mode, offscreen Mesa/llvmpipe): original
320x200 took **7.5 s**, software `--hires 3` took **19.1 s**, and
`--gl 3` took **15.8 s**. The new GL path itself submits only **6–7 draw
calls** for scenes with roughly 80–140 captured primitives (down from
about 90–160 calls before batching), and a typical GL capture has
0.2–0.4 ms draw submission plus 0.8–1.8 ms synchronous readback. These
timings are *not* from a real GPU and do not isolate all CPU costs. They
show why GL batching alone cannot remove the remaining cost of the
original rasteriser, CPU composition and SDL texture upload.

For widescreen or >60 Hz playback, a newly captured GL scene is now
finalised without first drawing/readback at the original width. The visible
presentation is the first GL rendering pass for that capture. This removes
one redundant readback every scene but **does not** remove the synchronous
readback and CPU-to-SDL upload on each presented frame. The host Graphics
submenu can switch 3D mode, widescreen, target FPS, detail and stats live.

## Pipeline

The original renderer keeps running unchanged (game state is identical
with and without `--gl`).  Per game frame:

1. `$2112c` (game frame): start a capture. On `--native off`, the PC hook
   at `$2fc14` also enters the scene and installs a hook at the original
   subroutine's observed return PC (`$2126e` in the supplied ADF) to finish
   the capture. In the native C++ path, `draw_scene_2fc14` calls begin/end
   itself. These routes are deliberately idempotent.
2. Every object the original draws announces itself when its LOD is
   chosen (`render_body` `$31e4a`, `distant_body` `$32dda` ->
   `glr::begin_object`). In interpreter mode, PC hooks recover the model
   and stream pointers from the original registers/stack at those labels.
   The static recompiler also returns to `Machine`'s dispatcher before
   each capture hook, using the addresses in `tools/recomp_hooks.txt`;
   without this, an internal C++ goto skipped the hook entirely.
   Flat models (mesh header +7 bit 1, not
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
   into an off-screen framebuffer of palette indices, with flat colours
   only (GL 2.1 or GLES 2; no logic ops, no `gl_FragDepth`):
   - clear to index 5 (sky);
   - the stencil bit "water" is set by the water polygons of the terrain
     tables (triangulated once, far grid outside the map, near grid
     inside; world space relative to the camera `(cell($5ac44) << 22 -
     $5ad5a, $5ad5e, cell($5ac46) << 22 - $5ad62)`, see
     `ground_sea_sky.md`) and inverted by every flat primitive whose
     operation inverts plane 1 (islands, lakes, runway markings);
   - a ground plane at Y = 0 is painted land (4) or water (6, or `$5ac4c`
     with `$5aaa8`) by the stencil bit;
   - the other flat primitives (runways, ground patches) in the original
     order, no depth test: their operation applied to land and to water
     gives two colours, drawn with the matching stencil test (one draw when
     equal);
   - 3D objects with the depth test, their operation applied to the land
     backdrop (3D primitives always write all planes).  Depth is
     logarithmic in vz, computed per vertex; the primitives are clipped at
     vz = 64 when captured so that only points in front reach it.  Later
     primitives of an object get a small depth bias (decals).
   The projection is the original's (`sx = 159.5 - 160 vx/vz`,
   `sy = 89.5 - 90 vy/vz`).
   Palette indices are encoded as `index / 15` in red and decoded using
   its high nibble after readback: this is exact for **both RGBA8 and RGBA4**.
   The former `index / 255` plus low-nibble readback silently lost colours
   on GLES2 RGBA4, and was susceptible to framebuffer quantisation.
   Set `FA18_GL_FORCE_RGBA4=1` to test the fallback even on desktop GL.
   With interpreter scene capture properly enabled, an off-screen Mesa
   replay of `tests/scenarios/free1.txt` at frame 4000 can verify the two
   framebuffer formats and their output. **Earlier** RGBA4/RGBA8 checks
   used an interpreter path that silently fell back to software hi-res,
   so those screenshots were not valid GL palette tests. The dedicated
   `tests/check_gl_scene.sh` checks that GL actually draws primitives in
   both interpreted and recompiled execution, that their snapshots match
   native C++, and that forced RGBA4 matches normal RGBA8.
   Captured 3D lines are camera-facing quads with a 1.5-output-pixel
   physical minimum and 0.55 original-low-resolution-pixel target weight
   at larger scales. This avoids both invisible hairlines and the earlier
   blocky full-pixel steps, without relying on optional GLES2 wide lines.
   3D points become view-facing triangle squares; all compatible 3D
   triangles share ordered batches with per-vertex palette and depth bias.
   Flat overlays are drawn as two ordered batches in disjoint land/water
   stencil classes, preserving the overlay order within each class.
5. The indices are read back into the hi-res layer of `hires.cpp`, whose
   composition is unchanged: pixels the game drew after the scene (HUD,
   cockpit) and 2D drawing during the scene that no 3D primitive explains
   come from the low-res frame, everything else from the layer, through
   the live per-row palette (fades, flashes).

## Plane operations in practice

Counted over free1, free2, mission7_combat, mission6_3, qual_carrier,
end_m3_ok, end_m6_ok and aero_all: 3D polygons, lines and points always
write all four planes; flat (ground) polygons use three partial
operations: invert plane 1 (land <-> water, runway markings 12 -> 14),
set plane 3 (runways: 12 on land), set planes 0+1 (7).  The plane copy
`$45014` did not occur.  This is what the flat-colour model above is
built on; an operation outside it is still drawn, resolved on land.

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

## Cockpit extraction and native composition

The original ADF contains `pix/inst5` (320x55) and `pix/frnt5`
(288x12), both ByteRun1-compressed 5-plane ILBMs with a transparent colour.
To extract the genuine artwork as separate RGBA PNGs without adding
copyrighted pixels to this repository:

```
python3 tools/extract_cockpit.py /path/to/FA-18.adf /tmp/fa18-cockpit
```

The two bitmaps are only the static instrument/backdrop art. Needles,
radar, fonts, indicators and flight-mode changes still have to be
reconstructed as foreground layers before OpenGL can stop reading the
original screen. The original assets now also have a **runtime C++ ILBM
decoder** (`src/native/cockpit_assets.cpp`) and a separate native cockpit
texture in the high-refresh/widescreen CPU compositor and the direct-GL
shader. Nothing generated from the ADF is committed.

`inst5` is placed at source (0,145); `frnt5` at (16,133). Together they
provide 21,056 static pixels. The renderer compares the live pixel against
both the original ILBM palette index and its CMAP colour (normalized from
the ILBM 16-step components to Amiga 17-step components). Matching pixels
are drawn from the native asset, resolved through the current row palette
when possible; the changed gauge, text, radar and marker pixels remain in
the original 2D foreground layer. This also preserves palette fades, since
nonmatching pixels fall back rather than forcing the original CMAP.

`FA18_COCKPIT_NATIVE=0` restores the old bitplane-backed composition for
pixel-exact A/B comparison; `FA18_COCKPIT_DEBUG=1` logs the number of decoded
and substituted pixels. `tests/check_gl_cockpit.sh ORIGINAL.adf` checks
equivalent flight and Pause screenshots. The native layer still uses the
original bitplanes to classify *dynamic* pixels — the next step is to
capture instrument writes directly and make the interface independent of
that classification. Afterwards, the original bitmap artwork can be
replaced with higher-resolution or fully 3D cockpit parts.

The first **dynamic** instrument is also reproduced natively:
`src/native/cockpit_instruments.cpp` reads player fuel from record +114,
formats the five-digit LCD at screen coordinates (104,183)–(122,187), and
generates its original 3×5 numeric font. This is drawn by the CPU composer or
uploaded in the direct-GPU foreground, rather than copied from the emulated
LCD pixels. The ILBM glyph ink/background indices are 16/24; the live LCD
uses their lower four bits, 0/8. To retain exact game-frame presentation
timing, the native digits replace the original ones only when the displayed
LCD still agrees with the current player value. Set
`FA18_COCKPIT_FUEL_NATIVE=0` to disable just this new instrument; use
`FA18_COCKPIT_FUEL_DEBUG=1` to log successful substitutions and cases where
the original redraw has not caught up. Matching the original LCD update
cadence without framebuffer checks is still outstanding.

## Open points


- The original interpreter's `$2fc14` had no scene-end callback before the
  return-PC hook was added. The OpenGL context could initialize, but no
  captured scene was rendered: `--native off --interpret --gl 3` produced
  *exactly the same pixels* as `--hires 3`. With the return and object-start
  hooks installed, the interpreter instead captures 3D primitives and the
  runway markings render as continuous polygons. Its GL frame matches the
  native path, whereas the software frame differs.
  The statically recompiled path likewise needs explicit dispatch barriers
  at every capture PC; after adding them its frame-4000 screenshot is
  byte-identical to the interpreter and native GL output.

- In external aircraft views, some wing and runway corners have dotted or
  blocky fragments. `FA18_HIRES_DEBUG=1` paints low-resolution fallback cells
  magenta; a `--gl 3` external-view capture (using scripted `key enter` for
  Keypad Enter) showed **no fallback cells** on the 3D geometry. These
  fragments originate in GL primitive rendering (potentially depth and
  overlapping edges), not the original 320x200 compositor. Driver-dependent
  wide lines have been removed; remaining dotted details in runway markings
  need investigation of the actual narrow polygon geometry and edge coverage.
  The HUD remains low-resolution by design. The current code still supports
  fallback for intentionally uncaptured 2D drawing and post-scene cockpit graphics.

- The order of toggles and overlays is approximated: all plane-1
  inversions are applied (stencil) before the overlays are painted.
- Primitive precision is the original's 16-bit view space; models are not
  smoother than in the original, only rasterised at a higher resolution.
- Discs (`$438a8`) are drawn as polygons in the view plane, points as
  square GL points; lines wider than 1 pixel are not available on every
  GLES driver.
