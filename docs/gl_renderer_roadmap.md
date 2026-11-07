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
  stats/FPS retain their separate top-left-anchored layer. The title now
  follows the original scanline's copper pen 9 rather than fixed amber,
  including palette fades. The rest of the Pause panel and instrument graphics still
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
- [x] Replace the native fuel LCD's 3x5 enlarged pixels with antialiased
  seven-stroke digits at 2x–8x output resolution. Retain the original
  per-bank refresh latches, palette-coloured LCD backing and the low-res
  reference toggle; compare bounded flight/dynamic changes, safe Pause
  fallback and RAM
  hashes, then check that direct GPU presentation matches the CPU composer.
- [x] Recreate the adjacent moving fuel bar as a true output-resolution
  instrument. Four scripted source-game tank levels recover a 4×23-cell
  gauge filled from the bottom, with `fuel*23/25000` truncated to source
  rows. The same per-bank writer latch already used for the LCD supplies
  the visible value; black backing and pen-4 green fill use live copper
  colours. Only this 4×23 rectangle is cleared; static side ticks remain
  intact. CPU and direct GPU share the vector fill without an extra draw
  call, and 1×/unusual-palette frames remain bitplane-exact. ADF-backed
  `tests/check_gl_fuel_bar.sh` measures source heights, high-resolution
  bounds, direct-GPU parity and unchanged emulated RAM.
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
- [x] Add high-resolution strokes for complete 3x5 readout digits in the
  seven remaining cockpit LCD fields. The original two-buffer source-font
  writer remains the redraw/position oracle, and its 0/4 or 5/0 copper pens
  provide colours. Partial or unknown digits fall back to exact original
  pixel-sized native glyphs. Both CPU and direct GPU presentation draw
  antialiased seven-stroke shapes; the GPU path reuses the existing overlay
  for stroke tokens. Test the three LCD regions, Pause fallback and unchanged
  emulated memory with `tests/check_gl_readout_vector.sh`.
- [x] Vectorise the radar's 16-pixel diagonal guides, four-pixel centre
  marker and moving purple contact inside the central cockpit panel. The
  guide/marker geometry is independent native code; contact location is
  captured from original pen-5 writer `$4446a` in the displayed bank.
  Complete original guide blits and marker writes now guard incomplete
  redraws per screen bank, without reading the source bitmap. GPU composition adds only
  uniforms to the existing pass. `tests/check_gl_radar_vector.sh` covers
  baseline/pull-back dot movement, panel bounds, RAM and GPU parity.
- [x] Intercept the Amiga radar pen-5 writer in both screen buffers and
  derive contact coordinates from its destination address and one-hot mask;
  use live copper palette ink instead of sampling the contact colour.
- [x] Recover the diagonal guide BLTSIZE writer `$447ee`, the four-pixel
  pen-12 marker writer `$444b0`, and the existing pen-5 contact writer
  `$4446a`. Capture all three in each original Amiga screen bank, including
  temporary one-pixel vertical offsets of the original radar stencil.
  The native radar has no source-bitmap visibility gate; optional
  `FA18_RADAR_BITMAP_REFERENCE=1` enables a legacy pixel-oracle comparison
  for regression testing. Palette indexes are 3 (guide: blitted plane 0
  over existing plane 1), 12 (marker), and 5 (contact).
- [x] Replace the 5x5 flight HUD aiming cross with native antialiased
  strokes at corrected 4:3 resolution. A small original-screen stencil
  establishes visibility and the live copper pen colour, including the
  progressive dimming into Pause; the vector shape itself comes from code.
  CPU composition and direct-GPU presentation draw the same strokes over
  the cleared old pixels. `FA18_HUD_RETICLE_NATIVE=0` restores the legacy
  bitmap, and `tests/check_gl_reticle.sh` verifies both flight and Pause.
  Both direct-GPU and SDL/CPU Pause headings now wait for the original title
  to appear, rather than flashing immediately at Pause-state entry.
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
- [x] Eliminate the redundant high-resolution **software** fill/line pixels
  while capturing an OpenGL scene; retain and compare the exact low-res
  coverage mask (including sliver/edge expansion), with a legacy reference
  toggle for 1x/4x framebuffer and RAM parity checks. The separate **Amiga**
  blitter still runs for now.
- [x] Reuse the GPU compositor's 2D fallback, copper palette and display-row
  mapping textures between interpolated high-FPS presentations of one game
  frame, including explicit rebinds after a bitmap presenter transition;
  retain a reference upload mode and Pause GPU/RAM parity test.
- [x] Recover the flight HUD's displayed 3×5 font ink from copper palette
  pen 13 once its writer has completed a glyph for the selected Amiga
  screen bank. Keep the frame/backdrop visibility gates and original-pixel
  fallback for incomplete redraws; verify bank flips, flight, Pause and
  dynamic text against the prior pixel-ink path.
- [x] Retint the native Pause heading from copper palette pen 9, using the
  original screen-space title solely as a visibility gate. Keep the cached
  vector coverage texture on the GPU; apply the pen colour as a uniform and
  match SDL/CPU blending. `tests/check_gl_pause_ink.sh` checks the exact
  source colour and that the heading waits for its original first draw.
- [x] Recover Pause heading visibility from the original **5×7 font writer**
  at `$47d56` (a separate source bank from the flight HUD's 3×5 font). Capture
  both decorative dashes and all six letters per screen bank, counting only
  newly drawn ink pixels. The Amiga framebuffer presents font writes one
  frame later, so exclude current-frame writes when evaluating the visible
  title. The resulting 0/5/23/64/87/…/133 count agrees exactly with the old
  bitmap scan; preserve it only as an optional reference. Test partial and complete titles against source screenshots
  and memory hashes with `tests/check_gl_pause_title_capture.sh`.
- [x] Recover the heading ticks' and aiming reticle's displayed ink directly
  from copper palette pens 13 and 10 after complete original writer captures.
  Preserve the HUD-frame/backdrop visibility gates and original bitmap
  fallback; verify four bank-flip, flight, Pause and dynamic scenes against
  the previous glyph-pixel ink sampling path.
- [x] Remove the remaining *source-pixel* HUD backdrop probes for completed
  original writer captures: compare ink with the corresponding copper
  background pen instead (5 behind upper text/heading/reticle, 12 behind
  G-meter/left units, 4 behind right unit labels). Keep the HUD-frame gate
  and early/incomplete source-glyph fallbacks. A reference toggle and a
  five-scenario screenshot/RAM test guard this migration.
- [x] Identify the HUD frame outline's **high-level original caller** at
  `$48eba`, rather than relying on generic bitmap blitter write/clear PCs.
  Its five ordered segments trace the left rail, left chamfer, crown, right
  chamfer, and right rail. Recover the current bank from the `A2` bitplane
  table, and latch only complete five-command sequences per bank. For recent
  redraws, infer outline visibility and ink from copper pens 10/8/0 (pen 5
  backdrop) with no sampled HUD-outline pixels. Retain bitmap recognition
  during transitions and before the first verified redraw; compare bit-exact
  images and RAM hashes in `tests/check_gl_hud_frame_capture.sh`.
- [x] Let a verified five-segment HUD outline remain active throughout Pause,
  regardless of the usual short flight-redraw freshness window. Eliminate
  remaining bitmap sampling for the Pause title's first-frame visibility and
  for its underlying background fill (live copper pen 5). Preserve three
  independent bitmap reference switches and verify the native paths against
  five Pause-entry/progressive/settled screenshots and unchanged RAM hashes in
  `tests/check_gl_pause_panel_copper.sh`.
- [x] Remove redundant visible-compositor GL driver work at high refresh:
  cache all shader uniform locations at link time (sampler bindings only
  need setting once), reuse one immutable fullscreen quad VBO instead of
  replacing the streamed scene buffer every present, and update the native
  HUD's vector/colour uniforms only when its complete captured state
  changes. The newer native fuel gauge, LCD and radar uniforms also reuse
  their captured game-frame revision across interpolated presents.
  The original dynamic 2D overlay/scanline upload and 3D scene
  paths are unchanged. `FA18_GL_PRESENT_CACHE_DEBUG=1` reports HUD uniform
  updates and reuses; `FA18_GL_PRESENT_CACHE_REFERENCE=1` forces the old
  every-presentation update for comparison. Use
  `tests/check_gl_present_cache.sh` for flight/Pause GPU pixel and RAM parity.
- [x] Keep the land/water vertex stream in its own GL buffer. Its exact
  camera-relative coordinates, altitude culling and detail flag form a
  reuse key; changing the view rotation or framebuffer size alone does not
  rebuild geometry. Island stencil toggles now rebind the same buffer rather
  than uploading it twice. `FA18_GL_GROUND_CACHE_REFERENCE=1` forces the
  previous rebuild cadence, while `tests/check_gl_ground_cache.sh` compares
  the resulting GPU output and emulated RAM. Terrain upload elimination on
  stationary poses is verified structurally; hardware speedup is not yet
  measured. A permanently static water mesh would additionally need a
  precision-safe world-space origin and an altitude-LOD strategy.
- [x] Reuse the high-refresh primitive interpolation scratch vector across
  successive presentations rather than allocating a new complete vertex
  stream each time. The interpolation formula and object pairing stay the
  same; `FA18_GL_INTERP_BUFFER_DEBUG=1` records capacity growth and reuse.
- [x] Retain separate captured GL batches for stencil toggles, flat overlays
  and ordered 3D triangles. A repeated serial/alpha reuses the existing GPU
  buffers, while a new interpolation phase repositions only XYZ with a
  source-vertex map, without repacking palette colours or primitive depth
  biases. Preserve the former ordered per-primitive fallback for any future
  non-triangle capture. `FA18_GL_BATCH_CACHE_REFERENCE=1` draws through the
  old pack/upload path; `tests/check_gl_scene_batch_cache.sh` compares
  flight/Pause direct-GPU pixels and emulated RAM against it. Hardware FPS
  improvement is not yet established.
- [x] Interpolate the **row-scaled camera orientation** using quaternion
  shortest-arc rotation and separately interpolated original row scales,
  rather than linearly blending nine world-to-view entries and shearing the
  ground plane. Exact capture endpoints and malformed/reflected matrix
  fallbacks retain the old values. `FA18_GL_CAMERA_INTERP_REFERENCE=1` and
  `tests/check_gl_camera_interp.sh` provide a 60 Hz screenshot and 120 Hz
  memory oracle; a unit test covers heading wrap and scaled orthogonality.
  View-space 3D object interpolation remains linear and still needs the
  same camera-pose decomposition work for fully rigid motion.
- [x] Erase the captured `$48eba` HUD frame and `$444a2`/`$44924` reticle
  directly from their original writer geometry, including completed-bank
  provenance in the HUD state. Crown/reticle backdrops come from the live
  copper pen 5 rather than neighbouring bitmap colours; original raster
  writes still execute and unknown banks keep the bitmap stencil.
  `FA18_HUD_ERASE_BITMAP_REFERENCE=1` restores the old colour-matched erase,
  while `tests/check_gl_hud_erase.sh` checks flight/Pause/bank GPU pixels and
  game RAM. The ADF-backed parity gate still needs to be run on a GL build.
- [x] Use the original captured source-font masks to remove old 3×5 ink for
  twelve HUD digits and five unit labels. The eleven source compass pixels
  also erase from their captured phase without comparing displayed bitmap
  colours. Current copper backdrops (pens 5, 12, 4) replace adjacent source
  samples; old per-pixel erasure remains for incomplete/stale banks and under
  `FA18_HUD_TEXT_ERASE_BITMAP_REFERENCE=1`. CPU and GPU share the clearing
  routine, with geometry unit coverage and a five-scenario screenshot/RAM plus
  direct-GL parity gate in `tests/check_gl_hud_text_erase.sh`. ADF replay
  validation is still pending on a machine with CMake/SDL/OpenGL installed.
- [x] Reconstruct the original front cockpit **behind the HUD rails** at
  x=85/233, y=138–144 from opaque `pix/frnt5` indices decoded at runtime,
  using each scanline's current copper palette instead of averaging adjacent
  Amiga pixels. Verified writer captures opt into this native asset backdrop;
  unknown banks and missing assets use the previous average. Reference toggle
  `FA18_HUD_RAIL_ART_BITMAP_REFERENCE=1`, focused unit test and a bounded
  screenshot/direct-GPU/RAM gate in `tests/check_gl_hud_rail_art.sh`.
  Actual GL/ADF replay remains unverified in this environment.
- [x] Replace the per-source-pixel 12-digit/5-label linear scans in both
  compositors with a one-time immutable owner lookup. This keeps the exact
  source geometry and visibility gates while removing 17 slot comparisons
  from the common 320×200 overlay scan. `gl_hud_glyph_test` covers all 255
  occupied source cells and every unassigned location; hardware speedup has
  not yet been measured.
- [x] Promote the original `pix/frnt5` **front-nose silhouette** to a native
  static artwork layer when a complete HUD outline was observed in the
  displayed Amiga screen bank. All nonblack ILBM cells at x=16..303,
  y=133..144 now use the original palette index and live copper colours,
  independent of the old bitplanes. Black ILBM background and unobserved
  banks keep the existing scene/bitmap composition; this prevents an
  opaque black rectangle from obscuring sky. The CPU and GPU paths share a
  mask with standalone unit coverage. Restore the old bitplane-matched
  path with `FA18_COCKPIT_FRONT_BITMAP_REFERENCE=1`; use
  `tests/check_gl_cockpit_front.sh` for ADF-painted-cell bounds, five
  flight/Pause/bank screenshots, direct GPU parity and RAM comparisons.
  Live GL execution is pending in this environment.
- [x] Unify the CPU and direct GPU **display-bank scene-row mapping** in a
  pure helper (including transitional invalid banks and per-edge EHB bits).
  Match visible completed scene rows even when the flight flag is briefly
  cleared. Correct the direct GL copper lookup: the live copper palette is
  uploaded for all original 200 scanlines, including cockpit instruments,
  while only the widescreen extensions clamp to the 3D viewport's last row.
  The previous GPU policy wrongly applied the clamp to native cockpit and
  readout pixels below y=144, even though the CPU path preserved their live
  scanline pens. A single shader uniform selects the side-only palette row;
  no extra sampler/readback is required. `gl_scene_rows_test` checks 1/145/
  168/180/200-row scenes, EHB, missing banks and transition cases.
  `FA18_GL_COPPER_GLOBAL_CLAMP_REFERENCE=1` restores the old policy, and
  `tests/check_gl_scene_rows.sh` will check GPU/CPU and RAM parity on Mesa.
  The new real-GPU regression has not been run in this environment.
- [x] Begin replacing the opaque lower `pix/inst5` instrument frame without
  relying on its current bitplane pixels. Static outer borders and a centre
  separator now resolve ADF indices through the live copper scanline; no
  asset data or extra GL texture is bundled. The eligibility gate requires
  the displayed bank's completed HUD frame *and* independent fuel writer,
  avoiding stale panels in other views. Dynamic attitude/radar/LCD/ECM
  regions still use their former compositing paths. The C++ fixture covers
  the safe mask, all sampled dynamic exclusions, opaque black and palette
  handling. `FA18_COCKPIT_PANEL_BITMAP_REFERENCE=1` restores bitplane
  matching; `tests/check_gl_cockpit_panel.sh` checks all changed ADF cells,
  five screenshot stages, direct GPU/CPU parity and RAM. The ADF runtime
  gate is pending a machine with SDL/OpenGL libraries.
- [x] Correct the instrument-panel ownership mask after visually inspecting
  `pix/inst5` at enlarged resolution. The former x=190..226 "static" region
  is actually a red/blue artificial horizon, and the rightmost purple cells
  are changing status indicators. Both now retain their dynamic overlay;
  the independently painted mask is restricted to 1,276 source cells,
  including 138 opaque black cells. The ADF-only mask check and focused
  unit fixture enforce those exclusions. A read-only per-displayed-bank
  mismatch audit reports six instrument areas with independent index/RGB
  change counts and bounding boxes (`FA18_COCKPIT_DYNAMIC_AUDIT=1`), with a
  screenshot/RAM no-side-effects regression for future writer tracing.
- [x] Add an independent original-bitplane visibility mask for visual
  debugging. Ctrl+B still suppresses original blitter writes and now blanks
  scanout; Ctrl+Shift+B, `--no-amiga-scanout` and script `scanout off|on`
  blank only the displayed pixels while letting all game writers run.
  `tools/scanout_diff.py` measures source-resolution per-region sensitivity
  between deterministic normal/hidden screenshots, optionally producing a
  red/gray heatmap. `tests/check_gl_scanout.sh` checks a full-flight RAM
  hash match and reports the original image's remaining contributions.
  The live GPU/ADF gate has not run in this environment.
- [x] Add region-selective original bitplane scanout masking for next-stage
  cockpit ownership debugging: Ctrl+Alt+B and `--hide-amiga-region` select
  upper view, nose, panel, or one of six moving-instrument rectangles,
  while preserving original DMA reads, per-row copper colours and RAM.
  `tests/check_gl_scanout_region.sh` compares screenshots and full RAM hash
  streams. The live GPU gate remains outstanding.
- [x] Recover the blue/red globe fill from the original ADF (pens 21/17,
  358 cells, source x=198..220/y=163..183) and isolate a portable
  high-resolution moving-boundary geometry model with signed distance,
  angle wrapping and edge coverage. ADF-backed and pure C++ checks validate
  its level pose and bounds (`docs/cockpit_globe.md`). This is a preparatory
  library component only; capturing the real moving writer, calibrating its
  input angles, and wiring it into native GL remain outstanding.
- [x] Run the original 68000 in a headless script replay independently of
  SDL/CMake/Nix, confirm live four-plane copper pens 5/1 instead of the ADF's
  five-plane 21/17, and validate three positions of the moving red/blue
  boundary against **all 358 original fill cells**. Original writer starts
  `$44d08` and `$451ac` both update plane 2 on both cockpit screen banks.
  `tests/check_globe_writer_probe.sh ORIGINAL.adf` is an end-to-end original
  program check, including a separate bank-only replay whose -20-degree
  native boundary matches all 358 pixels at two rolled poses. Full
  input-angle calibration and GL integration remain.
- [x] Recover the original globe's low-level call chain from real 68000
  flight execution: caller `$213a8` → rectangle `$44c42` → clipping
  `$45ce6` (from `$44c80`) → BLTSIZE `$44d08`/nested `$451ac`.
  The 21-row/two-word rectangle updates every four frames, alternating
  source state and screen bank. A bounded optional register/stack trace and
  regression now verify five consecutive redraws and the displayed row keys.
  Identifying the original attitude-to-source producer is still required
  before removing the Amiga writer itself.
- [x] Recover the original *double-buffer selector*, distinct from the
  attitude image: `$283d2` flips `$5a964`, and `$4417c`/`$44182` select
  descriptor pairs into `$5a9ae`/`$5a9b2`. The read-only RAM observer and
  headless replay regression verify all 15 pointer/selector writes across
  five redraw frames. This resolves the A2 alternation without claiming
  that it encodes roll or pitch.
- [x] Record transient vertical *whole-panel* movement during a rolling
  replay: at frames 4488 and 4492 the live fill moves -1/+1 source pixel,
  yielding 351 of 358 cells inside the 21-row capture. Source-mask
  alignment eliminates the apparent missing pixels, and a regression checks
  the displaced shape and the red/blue boundary fit independently. A second
  independent match of 910 pixels *outside* the globe proves the rest of
  the panel shifts -1/+1 with only two residual differences, then returns
  to its previous position by frame 4496. A matching no-roll control
  replay (zero pitch and roll, but descending) repeats the shifts, ruling
  out the turn as their cause. Original `$26b16` sets view-sequence mode 1
  on first touchdown (altitude 750, grounded bit `$80`), and `$39b06`
  drives `$5ac10` through `-40,+40,0,0` screen-byte offsets. The original
  `$39ade` table shows modes 1/3 share this animation; 2/4 invert it.
  The test verifies the binary table and original runtime values. The
  experimental globe path now captures the geometry and independent camera
  offset per screen bank, though full-panel native motion remains.
- [x] Separate the original lower-panel base rewrite from the rolling
  attitude-boundary draw by sampling both *live four-plane source banks* at
  each 68000 cockpit call boundary. `$45368` updates 268/282 panel bits in
  the rolled 4480/4484 frames (254/270 even without roll); `$44c42` then
  makes an additional 33/35 roll-sensitive changes against only one per
  no-roll frame. Exact source masks are inspectable in the probe, which
  also records the instruction-level four-blitter sequence in `$45368`.
  The touchdown camera's -40/+40/0 blitter inputs at code frames 4480/
  4484/4488 move the `$44c42` destination `D1` from its normal 158 to
  157/159/158, and reach the displayed panel at frames 4488/4492/4496. A replay
  regression checks both buffer phases. The upstream path is now known as
  attitude matrix (`$42bbe`) → sea projection (`$42f62`) → copied polygon
  (`$44b36`) → integer instrument scaling (`$44de4`). The opt-in native
  globe captures the scaled quad per display bank and recolours only the
  ADF mask with live copper pens; its word-aligned edge matches all but
  at most two pixels in five tested redraw/display pairs. Exact
  inverted-flight and fast-transition parity are outstanding, so
  original-bitplane rendering remains the default.
  `tests/check_gl_globe_native.sh ORIGINAL.adf` verifies 2× source-pixel
  fill survival with the old globe selectively hidden and unchanged RAM.
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
  against the static ILBM) and re-render them at output resolution. The
  original top HUD rail's generic blitter paths at `$44856` (write) and
  `$44a98` (clear) are now traced to the specific `$48eba` outline caller.
  Finish migrating other HUD/cockpit writers and transitions; even with the
  outline captured, unknown banks and stale active-flight banks intentionally
  use the bitmap; a completed outline is held through Pause.
- [ ] Validate sustained 120/240 Hz and seam stability on real GPU/monitor.
