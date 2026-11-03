# OpenGL renderer completion plan

This is an incremental replacement for the original 320x200 drawing path,
not a change to the 68000 simulation. Each step should preserve native,
recompiled and interpreted emulated RAM equivalence.

## Findings to reproduce

- The 120/180/240 Hz option can present very few extra frames: every extra
  presentation currently runs the whole GL scene through a hidden context,
  synchronously `glReadPixels` it, composites on the CPU and uploads it into
  SDL_Renderer again. Missed half-tick deadlines are simply skipped. The
  existing integration test checks *some* extra frames, not their sustained
  rate or actual motion during a long real-time flight.
- Widescreen side bands use the newest GL scene even when a displayed row
  belongs to a previous double-buffered scene; the centre has a different
  timebase and/or an original-renderer fallback. This can make the seam
  jump or flicker. The HUD/2D layer also changes palette/scanline timing.
- The statistics texture is rendered with a null destination rectangle,
  stretching its fixed 960x720 contents across *the entire* wider logical
  screen; it is not an anchored overlay.
- Full detail has many per-primitive GL draw calls/state changes, repeated
  full water-mesh upload, synchronous index readback and CPU conversion.
- The current GL line quad is precisely one **output** pixel wide (at
  `--gl 3`, only one third of a low-resolution pixel), too thin for the
  original game's art style.
- GL still delegates unclassified 2D drawing, HUD and cockpit to the
  Amiga bitplanes. Widescreen must therefore not claim to be a fully native
  scene compositor yet.

## Ordered implementation and acceptance gates

1. **Observe real presentation rate and cost.** Add per-stage GL timing
   and draw-call counters, plus actual presentation/capture counters to
   logs and a replay that measures sustained vs requested Hz. Keep a 60 Hz
   baseline and the 120 Hz memory-equivalence test. Do not treat the
   existing `extra presentation 1` log as proof of smooth animation.
2. **Stabilize the viewport.** Correct the stats overlay placement and
   width. Make side bands and the 4:3 GL centre use the same captured scene
   and palette timebase for 3D rows; draw the cockpit/HUD and unclassified
   2D elements in a separate top layer. Add tests for seam continuity,
   resize, and screen transitions (flight, menu, external camera).
3. **Reduce GL work.** Build contiguous triangle batches for compatible
   ground operations; use per-vertex colour/depth-bias for an ordered 3D
   triangle batch. Upload static water geometry once. Compare complete
   pixel screenshots before/after on Mesa and GLES2/RGBA4. The long-term
   target is a visible GL context and a GPU compositor, eliminating GPU
   readback and SDL texture re-upload for normal interactive presentation.
4. **Make interpolation genuinely smooth.** Render from timed capture
   snapshots rather than coupling presentation to a synchronous 60 Hz
   callback. Count achieved presents/second, report missed deadlines;
   schedule intermediate frames even when no new 3D capture occurs. Avoid
   object-ID collisions and account for angle wrap/camera orthogonality.
   Physics remains at the original rate. Test with a moving plane and
   camera at 60/120/240 Hz on real GPU hardware; measure motion, not only
   differing pixels in screenshots.
5. **Style the rasterisation.** Give lines a resolution-aware minimum
   thickness, with round/capped ends where appropriate; test various
   slants, endpoints, depths and intersections at 1x–8x. Avoid the older
   blocky, full low-resolution-pixel-width stair steps.
6. **Replace the old screen as background.** Capture UI and cockpit drawing
   into separately addressable layers (original bitplanes only as an
   initial comparison oracle). Decode the original cockpit assets from the
   ADF, match cockpit states/view modes, draw those textures ourselves,
   then progressively implement gauges and indicators as vector/text
   elements at native resolution. Only after this parity step should we
   explore cockpit parallax, 3D framing and newly visible widescreen art.
   Once parity is demonstrated, bypass the original CPU blitter/rasteriser
   *writes* for captured 3D primitives in GL mode, while continuing to
   execute the polygon operations that update game state. Check that no
   gameplay path reads the original 3D pixels before enabling the bypass;
   this could be a larger full-detail speedup than GPU draw-call batching.

Every implementation step: add a deterministic test or a measurable
reproduction, run `tests/check_gl_scene.sh` and
`tests/check_gl_interpolation.sh` against the supplied ADF, and if
`src/native/` changes run the 41-scenario `tests/check_native.sh` gate
before committing. Note limitations explicitly rather than masking them.

## Work started (2026-11-03)

- [x] Instrumented GL draw/readback time and achieved presentation rate.
- [x] Anchored the stats overlay and unified the 3D centre/side timebase.
- [x] Added a physically legible, resolution-aware stroke width.
- [x] Batched 3D triangles including view-facing point squares; batched
  land/water flat overlays while preserving their per-class draw order.
- [x] Compared full-detail costs: 3,700-frame offscreen Mesa replay was
  7.5 s low resolution / 19.1 s software 3x / 15.8 s GL 3x. Captured GL
  scenes went from about 90–160 draw calls to 6–7; the remaining cost
  is mostly outside those GL submissions.
- [x] Added a stdlib-only ILBM/ByteRun1 extractor for `pix/inst5` (320x55)
  and `pix/frnt5` (288x12). The images are original cockpit/instrument art,
  not the dynamic needle/text/radar layers. Extracted files are never
  added to git.
- [x] Defer the redundant 320x200-scale GL readback at scene-end when
  widescreen or high refresh will immediately redraw at output dimensions.
  A mode switch back to normal 60 Hz materialises the current capture on
  demand; an incomplete older double buffer uses original pixels until
  refreshed. This avoids one GL draw/readback per captured scene, **not**
  the remaining per-presentation readback or SDL texture upload.
- [x] Move host options into Graphics, Controls, and Extras menus. Widescreen
  and 60/120/180/240 render FPS are selectable at runtime.
- [ ] Eliminate synchronous readback and SDL upload in interactive GL mode.
- [ ] Make the compositor independent of the original HUD bitplanes;
  reconcile cockpit instruments and dynamic overlays as first-class layers.
- [ ] Validate sustained 120/240 Hz and seam stability on real GPU/monitor.
