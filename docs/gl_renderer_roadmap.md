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
  added to git. A 320x200 screenshot at VBL 4000 verifies `inst5` at (0,145):
  after converting ILBM 4-bit palette components from 16-step to Amiga
  17-step values, **17,332/17,600** pixels match the existing cockpit.
  `frnt5` at (16,133) has average 0.67 colour-channel error over its
  nonblack pixels (the next-nearest x offset is >5). These are the correct
  source rectangles for the next native GL cockpit texture layer; the
  differences need classification as dynamic gauges/markers or palette effects.
- [x] Defer the redundant 320x200-scale GL readback at scene-end when
  widescreen or high refresh will immediately redraw at output dimensions.
  A mode switch back to normal 60 Hz materialises the current capture on
  demand; an incomplete older double buffer uses original pixels until
  refreshed. This avoids one GL draw/readback per captured scene, **not**
  the remaining per-presentation readback or SDL texture upload.
- [x] Move host options into Graphics, Controls, and Extras menus. Widescreen
  and 60/120/180/240 render FPS are selectable at runtime.
- [x] Add an **opt-in visible GPU compositor** (`--gl-direct`, or
  `FA18_GL_DIRECT=1`): render captured geometry into the GL scene FBO, copy
  GPU-to-GPU to a scene texture and combine it with a 320x200 original 2D
  overlay, a 32x200 scanline palette, and a 1x200 scene-row map using a
  fullscreen shader. Normal presentations call no `glReadPixels` or
  `SDL_UpdateTexture`. At this initial stage the SDL compositor still handled
  menus and screenshots (menus were migrated below); GPU display remains
  experimental pending real-GPU testing. The Amiga 3D rasterizer still executes for
  emulated-RAM fidelity. `FA18_GL_DIRECT_COMPARE=1` deliberately enables a
  readback *for diagnostics* and samples every logical pixel centre against
  the CPU compositor; `tests/check_gl_direct.sh` gates 4:3/widescreen parity.
  Short `--gl 3 --widescreen --render-fps 240` real-time replay on Mesa
  llvmpipe: legacy path observed 176.6/168.5 presents/s (63/73 missed extra
  deadlines), direct path 226.0/241.0 presents/s (15/0 missed deadlines).
  These figures are not a substitute for prolonged real-GPU testing.
- [x] Present the remaining 2D-only screens on the *same visible GL
  swapchain*: pre-flight/gameplay transitions, native debug menu, mesh
  viewer and Original renderer mode now upload an RGBA bitmap through a
  small nearest-neighbour shader. The separate window-anchored stats/FPS
  layer stays on the GPU. The SDL window renderer remains for deterministic
  `--fast`, screenshot and GL-failure fallbacks only. This retires routine
  mixed SDL/GL swaps, **not** the old Amiga 3D rasterisation writes, whose
  gameplay read dependencies still need to be checked before bypassing.
  `tests/check_gl_bitmap_present.sh` checks pre-flight and runtime-mode
  change pixel parity, plus GPU overlay setup.
- [x] Project the widescreen 3D sides through all 200 source scanlines,
  including below the original cockpit's shorter 3D viewport, with the
  final scene palette reused beyond that cutoff. The original 320-column
  cockpit stays composited in front. CPU and GPU compositors use the same
  scanline and palette policy. Direct-mode side pixels have parity coverage.
- [x] Anchor the stats panel to the widened viewport's actual top-left,
  expose a measured `--fps` readout in Graphics, and composite both overlays
  using a native 960x720 RGBA texture in direct GL (uploaded only on game
  ticks, not each interpolated presentation). The debug replay verifies
  that toggling stats no longer falls back to the SDL compositor.
- [x] Preserve the last completed 3D scene when Pause/menu enters a game
  frame without a matching scene-end capture; avoid presenting the unfinished
  geometry as a blank frame.
- [x] Reproduce a **consecutive-frame** widescreen seam failure at 3510/3511
  during the scripted flight launch. The game clears its in-flight flag and
  displays an unfinished 3D bitplane buffer while the most recent complete
  geometry is still current. Recent GL captures now keep the sides visible;
  rows without any completed 3D layer are projected from that same capture
  instead of flashing the Amiga back buffer. The Pause state is explicitly
  excluded from this last rule to preserve its genuine original 2D graphics.
  `tests/check_gl_seam.py` checks those sky rows in adjacent screenshots.
- [x] Render the first independent native flight UI element: the Pause
  heading. Clear its old 320x200 text rectangle using the panel's live colour
  (including copper fades) and paint an antialiased vector heading in a
  separate 960x720 transparent UI texture, centred on the original 4:3
  region. This works in the CPU fallback and the opt-in direct GPU compositor;
  stats/FPS retain their separate top-left-anchored layer. The new title
  currently uses a fixed amber rather than tracking the original title's
  per-frame fade. The rest of the Pause panel and instrument graphics still
  use the original bitplanes. A direct-GPU paused comparison at VBL 3570 had
  one unrelated raster-edge mismatch (160,108) in 85,600 source pixels;
  `tests/check_gl_direct.sh` allows at most two on that transition.
- [x] Add the first asset-backed cockpit layer. The C++ ILBM/ByteRun1 loader
  reads `inst5` and `frnt5` directly from the user's ADF and builds a native
  320x200 indexed/RGB cockpit texture. CPU composition and the visible GL
  shader choose original asset pixels wherever the live screen still matches
  their index or CMAP colour; the original dynamic instrument drawing stays
  in front. A flight replay logged 19,292 static artwork pixels and
  1,764 dynamic/palette fallbacks out of 21,056 available at VBL 3482.
  Widescreen GL flight and Pause screenshots are pixel-exact against the
  previous compositor. `FA18_COCKPIT_NATIVE=0` supplies the deterministic
  legacy oracle for `tests/check_gl_cockpit.sh`.
- [x] Reproduce the first dynamic instrument from game state: the fuel LCD
  (`player +114 >>8`) at (104,183), using a code-defined 3x5 digit font
  equivalent to the ILBM's `21000` placeholder and the live copper pen
  colours 0/8. The original screen is only used as a timing/parity oracle:
  if its currently displayed digits lag the game value, keep that redraw;
  otherwise composite the generated LCD. In flight, the native five-digit
  renderer activates and produces an identical screenshot. See
  `FA18_COCKPIT_FUEL_NATIVE=0` and `FA18_COCKPIT_FUEL_DEBUG=1`.
- [x] Replace the fuel LCD pixel-parity timing oracle with a per-buffer
  native redraw latch. The original pixel writer at `$4744e` supplies the
  refresh event and destination; a recognized bitplane-3 LCD destination
  records the fuel value for that screen buffer. The actual displayed
  row pointer selects the correct five digits during compositing. Buffers
  not yet observed retain the original pixels until their first redraw.
  Pixel-exact replay at VBL 4000 additionally exposed and corrected the
  unusual last-row shape of the original font's digit `9`. The corresponding
  `tools/recomp_hooks.txt` boundary prevents recompiled blocks from skipping
  the instrument's original redraw instruction.
- [x] Trace the right-hand altitude and airspeed LCD fields and reconstruct
  their live glyph pixels in the native cockpit layer. The `$47080` /
  `$470f2` formatting routines describe the game values but are not the
  normal text-pixel redraw entry: the resident 3x5 font (offset table at
  `$528c0`) is blitted via `$47476` into bitplane 2, giving green pen 4
  over black pen 0. Its glyph-source pointer, destination word and shift
  yield the exact 240..265,158..170 digit positions, including blank glyphs
  used to erase earlier values. Native per-buffer masks now track this
  branch and compose the two numeric rows entirely from the source font
  and live copper colours; untouched/unseen pixels retain the old layer.
  `$47476` is a recompiled-code hook boundary. Verify with
  `tests/check_gl_cockpit.sh` and `FA18_COCKPIT_READOUTS_NATIVE=0`.
- [x] Extend font-writer capture to the throttle/Mach row (240..250,175..179),
  the left and right auxiliary rows (144..154 and 242..250,184..188), and
  the two far-right ECM numbers (292..307,180..184 and 188..192). The three
  lower green rows also use `$47476` and live pen 4/0. The ECM glyphs use
  `$4744e` in bitplanes 0 and 2 (the same writer as the fuel LCD but a
  different destination), producing black pen 0 text on purple pen 5. The
  font data and write destinations recover every glyph shape/position,
  retaining distinct masks for each Amiga screen buffer. Legacy bitplane
  pixels are never sampled for any of these seven fields. A new scripted
  F10-throttle replay (`tests/data/cockpit_dynamic.txt`) reaches airspeed
  2391 in the player record; its VBL 3960 cockpit is pixel-exact versus
  the original, as is the stationary VBL 3490 cockpit. The Pause state
  remains a separate parity case in `tests/check_gl_cockpit.sh`.
- [x] Replace the 5x5 flight HUD aiming cross with native antialiased
  strokes at corrected 4:3 resolution. A small original-screen stencil
  establishes visibility and the live copper pen colour, including the
  progressive dimming into Pause; the vector shape itself comes from code.
  CPU composition and direct-GPU presentation draw the same strokes over
  the cleared old pixels. `FA18_HUD_RETICLE_NATIVE=0` restores the legacy
  bitmap, and `tests/check_gl_reticle.sh` verifies both flight and Pause.
  The direct-GPU Pause heading is also delayed until the original title
  actually appears, rather than flashing immediately at Pause-state entry.
  The SDL/CPU Pause-heading path still uses the earlier state-only gate.
- [x] Rasterise the cockpit HUD frame's **top rail and two diagonal chamfers**
  at native corrected 960x720 resolution. The original top/corner pens are
  separately sampled as visibility and *live copper colour* oracles, so the
  new geometry dims along with the existing Pause fade. Only the 320x200
  rail/chamfer stencil is erased; both the CPU widescreen composer and the
  direct GPU shader draw matching antialiased segments. Use
  `FA18_HUD_FRAME_NATIVE=0` to compare the legacy bitmap and
  `tests/check_gl_frame.sh` for the bounded-diff flight/Pause check. The
  long black vertical frame rails were left for the next migration.
- [x] Move both **vertical HUD frame rails** into the same native vector
  compositor. Their old one-pixel black bitplane strokes at x=85 and x=233
  are removed only while the observed frame stencil is active. Above the
  cockpit lip, the erased pixels reveal the actual GL scene with its
  original double-buffered row mapping and live scanline palette; at the
  lip itself, reconstruct the hidden surface from its neighbouring cockpit
  pixels. Both the CPU and GPU compositors then draw black antialiased
  strokes at 960x720 coordinates, joining the native top chamfers. Toggle
  only these new rails with `FA18_HUD_RAILS_NATIVE=0`, or the complete
  frame with `FA18_HUD_FRAME_NATIVE=0`. `tests/check_gl_rails.sh` covers
  flight and faded Pause and rejects changes outside the two rail regions.
- [x] Render the 3D scene directly into the **colour texture attached to its
  framebuffer**, instead of copying the completed RGBA8/RGBA4 renderbuffer
  into a second full-sized texture on each `--gl-direct` presentation. On
  GL 2.1 and GLES2 use an unsized `GL_RGBA` texture; if its framebuffer is
  incomplete, retain the previous renderbuffer plus GPU-to-GPU copy path.
  Explicit `FA18_GL_FORCE_RGBA4=1` continues to use a genuine RGBA4
  renderbuffer; `FA18_GL_SCENE_TEXTURE=0` selects the previous implementation
  as a visual oracle. Cache up to four framebuffer sets (with their own
  depth/stencil and scene textures) across centred/widescreen resize requests
  so alternating capture dimensions do not reallocate GPU storage every
  presentation. A 3,650-frame offscreen widescreen/direct replay reduced
  framebuffer allocations from **322 to 4** (the four distinct sizes).
  `FA18_GL_SCENE_DEBUG=1` logs only newly created attachments;
  `tests/check_gl_scene_texture.sh` verifies flight/Pause pixel identity,
  RGBA4 parity, direct GPU pixel-centre agreement and allocation reuse.
  Restrict the vertical frame-rail shader to the two narrow x regions rather
  than evaluating segment distances across the entire HUD window.
- [x] Reconstruct the first eight visible flight HUD numerals as native
  antialiased strokes (two two-digit top fields, one speed/altitude digit
  apiece, and the G-meter pair). Validate each of the original 3x5 glyphs
  against the resident digit patterns before replacing it; absent, fading,
  or unfamiliar glyphs keep the original display. This is a bounded
  *intermediate* step: values/visibility still come from the current Amiga
  framebuffer while their vector shapes are native. The CPU and direct-GL
  presenters share the decoded stroke masks and live gray ink colour; both
  erase the original glyph pixels before rendering. Opt out with
  `FA18_HUD_DIGITS_NATIVE=0`; use `tests/check_gl_hud_digits.sh` for the
  flight/Pause changed-region and antialiasing checks.
- [x] Add the five flight unit letters `KT`, `FT`, `G` as native
  antialiased CPU/GL strokes, conditioned on complete source 3x5 letter
  stencils and each letter's current grayscale pen. These use the same
  frame-visibility check as the digits. Unknown/partial glyphs retain the
  original pixels. Test the exact shapes and rejections without the ADF in
  `gl_hud_glyph_test`, and bound live flight/Pause pixel changes with
  `check_gl_hud_digits.sh`. `FA18_HUD_LABELS_NATIVE=0` disables only labels.
- [x] Replace the five alternating heading tape ticks and the central heading
  index with narrow antialiased native strokes. Detect the moving 10-column
  tick phase only from a complete 11-pixel 320x200 stencil with one live
  grayscale pen; a partial/faded stencil retains the source HUD. The CPU
  composer and direct-GL shader use the same coverage/sample grid. Disable
  just this pass with `FA18_HUD_HEADING_NATIVE=0` and test with
  `gl_hud_glyph_test`, `check_gl_heading.sh`, `check_gl_direct.sh`.
- [x] Capture flight HUD digit/label values directly from the original
  68000 glyph writer `$47d56` (source bytes at D4/A0, destination A3,
  pixel shift D2). Record complete five-row redraws per screen buffer and
  use those values for the native CPU/direct-GL HUD. Retain a one-pixel
  colour/visibility probe for copper fades and the previous complete-stencil
  fallback until the displayed bank has been refreshed. Use
  `FA18_HUD_FONT_CAPTURE=0` to disable interception;
  `tests/check_gl_hud_capture.sh` checks output identity and real use.
- [x] Capture the eleven heading tape and fixed-index point writes through
  original `$444c0` (HUD caller A5, pen and per-bank destination word/mask),
  deriving the moving phase without source-screen shape comparisons. Require
  a complete recognised command set, use only a live grayscale pen probe,
  and retain the full bitmap-stencil fallback until a valid redraw is
  captured. `FA18_HUD_MARK_CAPTURE=0` reverts to stencil recognition;
  `tests/check_gl_heading_capture.sh` checks exact flight/Pause output parity.
- [x] Recover native aiming-reticle visibility from the original two-stage
  rasteriser sequence: `$444a2` sets four vertical points and `$44924`
  emits the five-pixel horizontal bar. Keep a per-screen-buffer latch for
  the complete command set, read only the current grayscale/copper pen for
  visibility, and fall back to the original bitmap stencil for unobserved
  redraws. `FA18_HUD_RETICLE_CAPTURE=0` disables the writer latch and
  `tests/check_gl_reticle_capture.sh` verifies flight/Pause parity.
- [x] Add an opt-in per-byte dependency audit for the original scene
  blitter's screen-plane writes, tracking subsequent blitter, display DMA,
  presenter and CPU/HLE reads through the shared bus (including cross-width
  accesses and later frames). Report candidate read PCs; leave every write
  intact. `FA18_GL_RASTER_AUDIT=1` and `check_gl_raster_audit.sh` provide
  the first measurable safety gate for a future writer bypass. See
  `gl_renderer.md` for what this audit does *not* prove.
- [x] Refine dependency tracing to separate guest read/modify/write
  instructions from read-only consumers, recording sample RAM addresses,
  during/after-scene counts and a BLTCON0/area/line/fill workload histogram.
  Move the original blitter's common minterms and area-fill prefix scan to
  exact native fast paths, with exhaustive Boolean tests and RAM-equivalence
  regression coverage.
- [ ] Expand the native text/UI capture path to the rest of Pause and the
  cockpit overlay, preserving palette/copper fades and separating dynamic
  gauges from original bitplane backgrounds.
- [ ] Replace the remaining bitplane read/modify/write dependencies of the
  HUD writer `$47d2c`→`$47d34`, `$47d4a`→`$47d56` and the point writer
  `$44456`–`$4445c` with native output layers; ensure screen-bank and
  copper fade timing before selectively bypassing old raster writes.
- [ ] Verify residual seam flicker under other transitions (views, terrain,
  Pause/resume) on real GPU hardware, including the 120/240 Hz present loop.
- [ ] Verify GLES2 render-to-texture completeness on device GPUs and explore
  an explicit RGBA4 texture-attachment format when the default RGBA texture
  consumes too much GPU memory.
- [ ] Make the compositor independent of the original HUD bitplanes;
  classify cockpit instrument writes directly (not by comparing bitplanes
  against the static ILBM) and re-render them at output resolution.
- [ ] Validate sustained 120/240 Hz and seam stability on real GPU/monitor.
