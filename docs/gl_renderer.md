# OpenGL renderer (`--gl N`, `--gl-direct`)

`--gl N` chooses the OpenGL **3D scene renderer** at N× resolution, but
normally sends its output through the CPU compositor and SDL renderer.
`--gl-direct` selects the visible-window GPU compositor for the same scene,
including native HUD/cockpit layers; it is not a separate 3D renderer.
On title screens, debug menus, screen transitions, the mesh viewer, and
after switching the 3D renderer to Original at runtime, a small GPU bitmap
presenter displays the remaining source image on the *same* visible GL
context. Stats/FPS remain an independent top-left GPU texture. The visible
window does not routinely alternate between SDL and OpenGL swaps;
`--fast`, screenshots and GL failures still use the SDL compatibility path.
`FA18_GL_BITMAP_COMPARE=1` checks every source pixel centre on this route,
and `FA18_GL_BITMAP_DEBUG=1` logs bitmap presentations and GL errors.
For convenience `--gl-direct` alone starts 3× OpenGL. Use
`--gl 4 --gl-direct --widescreen --render-fps 120` for the current enhanced
path at 4×. Without `--gl-direct`, choosing "4x OpenGL" in the Graphics
menu still uses the same scene renderer, but the SDL/CPU presenter.
The menu shows GL output as a read-only startup setting: an SDL window
created without an OpenGL context cannot switch to direct GPU presentation
at runtime. The active path is logged on startup as `graphics: ...`.

Widescreen presentation holds the most recent *completed* 3D capture while
the Amiga draws into another buffer. The CPU and GPU compositors both retain
that completed layer, and rendering an interpolated frame never prematurely
finishes a partial capture. This avoids exposing a blank back buffer between
3D redraws. `FA18_GL_PRESENT_DEBUG=1` reports any fallback to the original
framebuffer; `tests/check_gl_widescreen_stability.sh` checks consecutive
presentations from a scripted flight.

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
and palette conversion time. This describes the default CPU/SDL display
path; the experimental `--gl-direct` compositor renders to the visible GL
window without the per-presentation CPU readback (see below and
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
readback and CPU-to-SDL upload on each presented frame in the default path.
With `--gl-direct`, the completed scene is already in the compositor's
own FBO colour texture, which eliminates both transfers and the previous
full-sized GPU-to-GPU copy. Its fallback uses the old renderbuffer and copy
when a driver rejects a texture colour attachment, or when explicitly
selected with `FA18_GL_SCENE_TEXTURE=0`.

The renderer caches four sets of GL colour/depth/stencil attachments to
avoid reallocating them when the original 4:3 scene and widened output
alternate in size. `FA18_GL_SCENE_DEBUG=1` reports new framebuffer
allocations, and `tests/check_gl_scene_texture.sh` verifies identical flight
and Pause pixels against the original path. Forced `FA18_GL_FORCE_RGBA4=1`
still selects a real RGBA4 renderbuffer for strict-format testing; the
unsized RGBA texture path and fallback both use only GL 2.1/GLES2 entry
points. GLES2 hardware testing remains outstanding. The host Graphics
submenu can switch 3D mode, widescreen, target FPS, detail and stats live.

## Audit before suppressing the old 3D rasterizer

`FA18_GL_RASTER_AUDIT=1` enables an optional chip-RAM dependency observer.
It marks original blitter writes to the four active 3D bitplane ranges while
a GL scene is being captured, then tracks reads of those bytes until the
next overwrite. At shutdown it reports scene write volume and tagged reads
by consumer: blitter (during and after scene), display DMA, CPU/HLE/other
host accesses, and the hi-res presenter. Candidate CPU read PCs are listed
for disassembly; an address alone does not prove that game logic consumes
the pixel value. Each marked access is tracked at byte granularity, so
crossing word/longword reads are counted. This is a diagnostic, not a
performance option: every original write is still performed.

The trace is conservative in *both* directions. It marks all blitter writes
inside the scene's screen-plane ranges, including any 2D elements drawn
before the scene ends, but it does not mark any direct CPU bitplane write.
The same bus is also used by guest, HLE, and renderer helpers; the category
`guest/host` needs inspection before being treated as a game-logic read.
Reads by the Amiga display DMA and later bitplane compositing are expected
and cannot by themselves justify dropping the original pixels. The
original 3D rasterizer must not be bypassed until its dependent consumers
have been replaced or isolated, including blitter read/modify/write and
HUD/cockpit compositing. A state load invalidates all outstanding marks.
Use `--native on` or `--native off` for measurements: `--native verify`
replays the same guest operations for equivalence checking and inflates
the read counts.

`tests/check_gl_raster_audit.sh ORIGINAL.adf [fa18-executable]` runs the
same real flight with the observer off/on and compares both the RAM hash
stream and the displayed screenshot. The stand-alone
`ram_access_observer_test` checks width, endianness and 24-bit address
wrapping of the generic memory observer without requiring the original ADF.

On offscreen Mesa, the 3,550-frame `direct_flight.txt` interpreter replay
observed 147 scenes and 4,353,921 original scene blitter word writes. Of
the previously marked words, the blitter subsequently read 2,412,492
during scenes and 43,229 outside them; display DMA read 12,700,305 outside
scenes. There were also 12,280 `guest/host` reads. Decoding the leading
candidate PCs with `tools/recomp.py` shows `$47d4a` / `$47d2c` load longwords
from A3 and `$44456`–`$4445c` perform bitplane read/modify/write operations.
These are concrete dependencies to classify before removing raster writes.
The trace does **not** establish that every scene word must remain in RAM;
many reads are screen presentation or redundant rendering, and many writes
hit the same word repeatedly. The observer's own overhead is excluded from
any GL-performance comparison.

The audit now also reports example bitplane RAM addresses and counts
read/modify/write pairs on the same instruction or a nearby subsequent
instruction (no more than `$20` bytes forward, same RAM address and width).
In a 3,540-frame flight, `$44456`, `$44458`, `$4445a`, and `$4445c` each
wrote back every tagged word they read (544–560 operations per PC).
Decoding the HUD font loop revealed that `$47d2c` reads a destination
longword which `$47d34` updates, while `$47d4a` reads another word whose
writeback is at `$47d56`: they are also bitmap RMW operations, just spread
across several 68000 instructions. The trace reports both PCs, the pair
count and a sample RAM address. The 3,550-frame flight replay matched
2,986/2,986 and 3,706/3,706 reads to their respective writes. These are
renderer-originated pixel mutations, not evidence of game logic inspecting
the scene.

That replay also counted the original blitter's work **inside captured
scenes**: 6,896,908 area words, 1,087,316 line steps and 1,108,368 fill
words. The most common BLTCON0 minterms were `00`, `0c`, `f0`, `fc`, `fa`,
`4a`, and `0a`. These have native Boolean fast paths. Exclusive and
inclusive area fill use a five-shift prefix XOR instead of testing each of
the word's sixteen bits in a loop; both forms are exhaustively tested for
all 65,536 input words and both incoming carries. This accelerates the
still-required original bitplane writes while preserving the CPU-visible
blitter state, rather than bypassing writes prematurely. End-to-end
performance gains remain to be measured on a production Release build and
real GPU hardware; the deterministic test verifies correctness, not speed.

In GL mode, the independent hi-res software rasterizer now keeps only its
320-column scene coverage mask; it no longer paints an unused high-resolution
colour buffer alongside the GPU triangles. This does **not** change any
emulated Amiga bitplane writes, which remain necessary for the HUD, copper
scan-out and a conservative 2D fallback. The old software path can be enabled
with `FA18_GL_COVERAGE_REFERENCE=1`, and `FA18_GL_COVERAGE_DEBUG=1` prints
per-capture FNV-1a coverage hashes. `tests/check_gl_coverage.sh` compares
every mask, resulting screenshot and emulated RAM stream at 1x and 4x against
the old path. Non-GL `--hires` retains the full software pixel rasterizer.
In a 3,550-frame deterministic interpreter replay using `--full-detail
--gl 4 --gl-direct` on headless Mesa llvmpipe, this reduced wall time from
27.27 s to 23.76 s (12.9%). That includes emulation, software-GL rendering
and presentation, not merely the removed raster work; it is not a benchmark
of native hardware GPU performance.

For `--gl-direct --render-fps 120|180|240`, the GPU reprojects an interpolated
scene between original game ticks. The original 320×200 overlay, 32×200
scanline copper palette and 1×200 double-buffer row map do not change within
a tick; they are now uploaded once per game-frame revision and merely rebound
on subsequent presentations. The bitmap/title presenter can change texture
bindings, so reuse still explicitly rebinds all three units. Setting
`FA18_GL_LAYER_UPLOAD_REFERENCE=1` restores every-presentation uploads;
`FA18_GL_LAYER_UPLOAD_DEBUG=1` logs upload/reuse totals. The deterministic
visible-GPU replay `tests/check_gl_layer_upload.sh` exercises both paths and
checks RAM hashes, 120 Hz extra presentations and identical GPU/CPU parity
results with and without caching at Pause (including the bounded 1x title
sample-centre difference). `tests/check_gl_direct.sh` retains its separate
tighter 3x Pause parity check.

The flight HUD's double-buffered font hook also gives the native renderer a
complete 3×5 source glyph for each number and unit label. An Amiga pen-5
background gains bitplane 3 during the glyph write (the G-meter uses pen 12),
and the displayed ink resolves to **copper palette pen 13**. Once the hook
has seen a complete redraw for the visible bank, the native HUD derives the
glyph colour from that scanline's palette entry instead of sampling a glyph
pixel from the original bitplanes. The background contrast test also uses
the original fixed *backdrop pen indices*, not a sampled source pixel:
pen 5 for upper HUD text/heading/reticle, pen 12 for the G-meter and left
unit label, and pen 4 for the right unit label. The contrast computation
still follows the copper fades independently on each scanline. The original
HUD-frame visibility gate remains in place. Unobserved and incomplete redraws
still use original glyph recognition. `FA18_HUD_PALETTE_REFERENCE=1` restores
the previous pixel-ink path for comparison. `tests/check_gl_hud_palette.sh`
checks pixel-identical bank-flip, flight, Pause and dynamic cockpit screenshots
and unchanged RAM hashes against that reference mode.
`FA18_HUD_BACKDROP_REFERENCE=1` independently restores the previous
source-background-pixel contrast checks for all captured text, heading and
reticle. `tests/check_gl_hud_backdrop.sh` compares five scripted scenarios
and their RAM hashes against that reference. Neither option is required
to run the game.

The independent vector **Pause heading** also follows the original game's
displayed colour: its glyphs use copper palette pen 9 (original Pause frame
3570: `#884400`), while their geometry is held in a static 960×720 alpha
texture. The GPU compositor changes only the tint uniform when the copper
palette fades; the SDL/CPU compositor blends the same per-frame ink. Both
paths wait for the original title to become visible, rather than displaying
the native heading as soon as the Pause state begins. The panel's other
elements still come from the original Amiga display. Run
`tests/check_gl_pause_ink.sh ORIGINAL.adf` to verify original/source colour,
delayed visibility and emulated RAM parity.

The heading's *visibility* no longer depends on scanning the original Pause
pixels in settled presentation. A memory watch on the title's bitplane-3
word found writes at `$47d56`, shared with the flight HUD's character
renderer, but using a different **5×7 font** from `$52b00`–`$52c10`.
The original draws two short dashes around `PAUSED` and writes the heading
progressively into both Amiga screen banks. Each captured row contributes
its five source-glyph bits to a per-bank coverage mask, suppressing duplicate
writes. The displayed frame lags the writer by one emulated frame, so the
compositor uses the count *before* any writes in the current frame.
The observed ink counts at displayed frames 3552–3558 are respectively
5, 23, 64, 87, 107, 128 and 133, exactly matching the previous framebuffer
count. The 24-pixel visibility threshold therefore still turns on at frame
3554, never at Pause-state entry. Copper pen 9 still supplies the colour.

No bitmap inspection is needed to decide title visibility, including on the
first Pause frame before any glyph has been captured: the heading remains
hidden until the original writer's observed coverage passes the threshold.
`FA18_PAUSE_TITLE_BITMAP_REFERENCE=1` forces the old source-pixel visibility
gate and `FA18_PAUSE_TITLE_CAPTURE_DEBUG=1` prints both values and the
individual glyph captures. `tests/check_gl_pause_title_capture.sh ORIGINAL.adf`
compares seven partial and complete title frames pixel-for-pixel, verifies
emulated RAM hashes, and checks all 21 displayed-frame coverage counts.
leaving Pause invalidates all per-bank title counts.

The Pause heading's backing rectangle no longer samples an original bitmap
pixel to erase the old letters. The compositor fills it with live copper
palette **pen 5**, evaluated for each displayed row, in both the SDL/CPU
and GPU paths. `FA18_PAUSE_BACKDROP_REFERENCE=1` restores the source-pixel
background for comparison. Unlike active flight, where only recently drawn
outlines are trusted, a verified complete five-segment HUD outline stays
available throughout Pause: the original does not need to redraw it every
16 frames. `FA18_HUD_FRAME_BITMAP_REFERENCE=1` independently restores the
old outline bitmap test. `tests/check_gl_pause_panel_copper.sh ORIGINAL.adf`
compares five frames across Pause entry, progressive heading draw and a
settled Pause, with all three bitmap reference switches set; it also compares
their emulated RAM hashes and confirms that the native paths are actually
selected. Leaving Pause invalidates all per-bank title counts.

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
5. In CPU/SDL presentation, the indices are read back into the hi-res layer
   of `hires.cpp`: pixels the game drew after the scene (HUD, cockpit) and
   unexplained 2D drawing during the scene come from the old screen,
   everything else from the layer through the live per-row palette.
   `--gl-direct` instead samples the scene texture and resolves the same
   overlays, copper palette and double-buffered row mapping on the GPU.

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
uses their lower four bits, 0/8. A pixel comparison at VBL 4000 also
recovered the LCD font's unusual `9`: its last row is only the rightmost
pixel, not a three-pixel bar. Original instruction `$4744e` writes the
fuel glyph's first bitplane-3 word. A PC hook observes that instruction and
its destination, associating the current player fuel with the corresponding
double-buffered screen row pointer. The presenter selects the latched value
using its actual row pointer at scanline 183. This preserves delayed LCD
refreshes **without inspecting any original fuel digit pixel**. Before a
given buffer has been identified and refreshed, the original foreground
remains as a conservative fallback. The address also appears in
`tools/recomp_hooks.txt` to force an instruction boundary in the statically
recompiled execution path, where internal instructions would otherwise
skip PC hooks. Set
`FA18_COCKPIT_FUEL_NATIVE=0` to disable just this new instrument; use
`FA18_COCKPIT_FUEL_DEBUG=1` to log discovered screen buffers and redraw
latches. The original code still executes to supply the refresh event; the
next step is an independent native instrument scheduler and further gauges.

At enhanced 2x–8x resolution, the five captured fuel digits are also rendered
as antialiased native seven-stroke glyphs directly at output resolution.
The old 19×5 source cells are restored to their live copper-coloured LCD
backing first; both the SDL/CPU compositor and the direct GL shader then
draw matching high-resolution strokes using the captured five-digit value.
The vector layer is enabled only when the font writer has refreshed the
visible bank and all five ink scanlines agree; elsewhere the exact 3x5
bitmap remains. `FA18_COCKPIT_FUEL_HIRES=0` selects the original 3x5 output
for pixel-exact comparisons. `tests/check_gl_fuel_vector.sh` confirms that
only the fuel LCD rectangle can change in flight, Pause and a dynamic throttle
replay, with matching original emulated RAM hashes. A Pause frame with no
eligible captured glyph/pen stays pixel-exact.

The adjacent vertical **fuel bar** is also independent of the original
bitplanes at enhanced resolutions. Its 4×23-cell interior is (111,158) to
(114,180); the green instrument ticks on either side are separate static
cockpit artwork. Four original-game replays with 24,998, 18,748, 12,498 and
6,248 fuel units produce respectively 22, 17, 11 and 5 filled rows. This
establishes the original integer scaling, `min(23, fuel * 23 / 25000)`.
The per-screen-buffer LCD refresh latch supplies the *displayed* value,
and the copper palette supplies the black backing and green pen-4 fill.
The CPU and GPU compose a narrow antialiased rectangle at output resolution;
the GPU path needs no additional texture or draw call. The exact 1× bitmap
is untouched, and unusual palette/partially drawn frames keep their
bitplanes. `FA18_COCKPIT_FUEL_BAR_NATIVE=0` restores the pixel-size source
bar; `tests/check_gl_fuel_bar.sh` checks four 1× source heights, bounded
high-resolution changes, RAM hashes and direct-GPU parity.
Under scripted `--fast`, normal visible GPU swaps are skipped; setting
`FA18_GL_DIRECT_COMPARE=1` explicitly enables those swaps for the parity
diagnostic, so headless tests actually exercise the direct shader.

Seven additional numeric fields are now synthesized from the original
resident 3×5 font without sampling their displayed bitmap pixels. The
original `$47476` branch supplies the five green flight/auxiliary fields
at y=158, 166, 175 and 184 (pen 4 ink on pen 0 background). The same
`$4744e` writer used by fuel draws both far-right ECM coordinates at y=180
and 188, but into bitplanes 0 and 2: their font-shaped ink is black pen 0
over purple pen 5. The source font pointer, original write destination and
horizontal shift reconstruct each 3×5 glyph exactly. Each screen buffer
retains its own captured mask until displayed, and the compositor reads the
live copper palette for every row. `FA18_COCKPIT_READOUTS_NATIVE=0` disables
only this font-derived layer. `tests/check_gl_cockpit.sh` compares flight,
Pause and an F10-throttle replay with a nonzero speed against the original.

At 2x–8x, complete five-row digit writes to these readouts also produce
antialiased 960x720-space seven-stroke digits. The source rows must match a
recognised numeral *and* the source-font ink/background pens for all 15
pixels; partial, overwritten and unrecognised glyphs stay pixel-exact.
Green readouts retain copper pens 4/0 and the purple ECM LCDs retain pens
0/5. The visible GL compositor stores compact stroke/origin tokens inside
its existing cockpit overlay instead of allocating another texture or draw
call. `FA18_COCKPIT_READOUTS_HIRES=0` restores the original 3x5 shapes;
`FA18_COCKPIT_READOUT_VECTOR_DEBUG=1` logs the first accepted digit bank;
`tests/check_gl_readout_vector.sh` bounds all changed pixels to the LCDs
and compares the unchanged game RAM.

The **central radar** is the next native dynamic cockpit layer. Comparing
ADF-extracted `pix/inst5` artwork to scripted flight frames shows that its
bezel/background are static, while two 16-pixel diagonals, a four-pixel
centre marker and a single blue-purple contact are drawn by the game. The
native code reproduces the guides and marker from geometry, and renders a
small antialiased contact at the position captured from the *original pen-5
pixel writer* `$4446a` (initially `(150,174)`, briefly `(166,160)` during a
pull-back). Its four instructions OR planes 0 and 2 and AND planes 1 and 3:
`OR.W D4,(A3); AND.W D1,(A2); OR.W D6,(A1); AND.W D3,(A0)`.
The plane-0 destination in `A3` and one-hot mask in `D4` identify the
coordinate and destination bitmap. The capture is latched separately for
each screen bank; the displayed row pointer selects which contact to show.
The position is **no longer searched for in the original bitplanes** or
guessed from physics state; its ink comes from live copper palette pen 5.
The **guide** is not a sequence of CPU pixel writes: `$447ee` starts two
16-pixel Amiga line blits by writing `BLTSIZE` (a `D4` low word of `$0402`).
`D7` addresses plane 0, the high nibble of `D5` places the start at x=139
or x=177 and `D1` selects the opposite slopes (`$11`/`$15`). The original
bitmap has a plane-1 mask beneath both lines, so adding plane 0 gives
**palette pen 3**; this was checked against a dump of the original RAM,
including the unaffected pen-0 backing. The **marker** uses the four
original pen-12 CPU pixel writes at `$444b0`, matching an inverted T.
Both blits and all four marker writes must complete in the displayed
screen bank before the native shape is shown. The blitter and marker
coordinates can shift by one pixel vertically during cockpit transitions;
both CPU and GPU geometry follow those per-bank coordinates.

The production path now depends on original **writer events and palette**,
not a source-pixel stencil. It still respects the existing game-state and
cockpit-mode gates and falls back if a writer has not been observed for
the displayed bank. `FA18_RADAR_BITMAP_REFERENCE=1` temporarily restores
the earlier bitmap-stencil gate for a diagnostic A/B replay (it is not
necessary to run the game). `FA18_RADAR_WRITER_DEBUG=1` logs contact points,
`FA18_RADAR_BLIT_DEBUG=1` logs guide blits, and
`FA18_RADAR_STENCIL_DEBUG=1` logs completed marker groups.
CPU and GPU draw directly at 2×–8×
with no new GL texture or draw call; 1× stays pixel-exact.
`FA18_COCKPIT_RADAR_NATIVE=0` restores source radar pixels at every scale.
`tests/check_gl_radar_vector.sh` covers both original screen-bank capture
sequences, two contact locations, legacy bitmap-gate equivalence, bounded
output differences, unchanged emulated RAM and direct-GPU parity. The latter
sets `FA18_GL_DIRECT_SUBPIXEL_PARITY=1` to compare corresponding subpixel
centres in the 600-row CPU buffer and 720-row aspect-corrected drawable;
the old logical-centre diagnostic remains the default for previous tests.
Comparing a CPU pixel's middle subpixel against every rescaled drawable
sample is insufficient for verifying hairline antialiasing.

The flight HUD's eight numeric glyphs at source positions (140,53),
(144,53), (160,53), (164,53), (106,84), (226,84), (96,121) and (102,121)
now have a limited native vector counterpart. Unlike the cockpit's hooked
redraw path, these values are *recognised* from the displayed 3x5 stencil:
only a complete match against the ten original digit patterns is accepted.
The native layer erases the matching old glyph pixels and paints the number
as antialiased seven-segment strokes in aspect-corrected 960x720 coordinates;
the digit `1` is a centre stem and `9` omits its bottom bar, matching the
source font. Unrecognised/intermediate/faded frames keep original pixels.
The five fixed unit letters also have native stroked shapes: `KT` at
(102,91)/(106,91), `FT` at (222,91)/(226,91), and `G` at (108,121).
Their exact original 3x5 stencils (K=5/5/6/5/5, T=7/2/2/2/2,
F=7/4/7/4/4, G=3/4/5/5/3) and their own live gray pen colours are
checked before replacing anything. A partially drawn or faded label
keeps its original bitmap instead of acquiring premature native ink.
The CPU and GL-presenter paths use the same visibility flags, digit masks
and individual label colours without extra GL texture uploads.
`FA18_HUD_DIGITS_NATIVE=0` disables the numeric and letter replacements;
`FA18_HUD_LABELS_NATIVE=0` disables only letters. Remaining text and the
rest of the Pause interface still use original bitplanes. This visual
bridge is not yet a replacement for the underlying game HUD formatter.
`tests/gl_hud_glyph.cpp` checks both pattern families and rejection of
partial/incorrect-colour glyphs without game data;
`tests/check_gl_hud_digits.sh` checks the flight/Pause regions with an ADF.

The numeric and label **values now have an independent original-code source**.
Unlike the green cockpit LCD's `$47476` branch, the gray flight HUD is drawn
by `$47d56` into bitplane 3. The glyph base in `D4`, advancing byte pointer
in `A0`, destination in `A3`, and horizontal shift in `D2` identify every
row of the 12 digit cells and five labels. Capture waits for all five rows
and associates each result with the correct double-buffered Amiga screen
base, so refreshing an offscreen bank cannot change the displayed number.
The native CPU compositor and direct-GL presenter receive the same captured
values; complete redraws derive their copper-faded gray ink directly from
the current scanline's palette pen 13. Full 3x5 bitplane-stencil decoding serves as
the fallback before a buffer has a completed capture, or if the visible
colour check fails. `FA18_HUD_FONT_CAPTURE=0` restores the previous
stencil-only logic. `FA18_HUD_CAPTURE_DEBUG=1` reports completed glyphs and
capture use, and `tests/check_gl_hud_capture.sh` compares flight/Pause
output against the old renderer pixel-for-pixel. This removes the source
bitmap as a *value or ink* oracle; broader HUD visibility and
original clears are still mediated by the source bitmap.

The five heading-scale marks beneath the top numerals are now native as well:
three two-pixel major ticks alternate with two single-pixel minor ticks,
10 source columns apart, and a fixed three-pixel central heading index at
(159,63..65). The original `$444c0` point writer provides the moving
phase directly from its eleven bitplane-3 write commands: the destination
word in `A0` and one-bit mask in the low half of `D4` locate the written
pixel. Its HUD caller context (`A5=$5a966`, pen 5 in upper `D4`) distinguishes
the compass from other pixels sharing this routine. An exact set of all
eleven points, including the fixed centre, is required per redraw and per
screen buffer; incomplete or unfamiliar commands are rejected. The CPU and
direct-GL passes share this captured phase and derive its live grayscale ink
from copper palette pen 13, using the copper pen-5 backing as a visibility
guard. Until a complete writer capture is available (or when its palette
contrast check fails), the previous full 8+3-pixel
stencil decoder remains the fallback. The passes erase only validated source
pixels and draw narrow antialiased strokes at corrected 960x720 coordinates;
the shader snaps its coverage to the same 3x source grid for exact GPU/CPU
parity in the scaled 1280x718 viewport. Disable this layer independently with
`FA18_HUD_HEADING_NATIVE=0`; set `FA18_HUD_MARK_CAPTURE=0` to use only the
prior stencil decoder, or `FA18_HUD_MARK_CAPTURE_DEBUG=1` to trace original
point-writer captures and compositing. `$444c0` is a recompiler hook boundary
in `tools/recomp_hooks.txt`. `gl_hud_glyph_test` checks complete, partial and
unexpected write sets; `tests/check_gl_heading_capture.sh` compares capture
and stencil output pixel-for-pixel in flight/Pause;
`tests/check_gl_heading.sh` bounds the native changed regions; and
`tests/check_gl_direct.sh` checks actual GPU presentation parity.

The 5×5 aiming reticle at (157..161,71..75) is also backed by its
**original drawing commands**. The original `$444a2` branch plots its four
vertical pen-10 pixels at x=159, rows 71/72/74/75, while `$44924` writes
the complete five-pixel horizontal bar at y=73. The destination and caller
registers (`A0`, `D7`, `A4`, `A5`, `D4`) distinguish these draws from the
other geometry sharing those rasterisers. Both are latched per Amiga screen
buffer, and the native CPU/direct-GL stroke is enabled only after the
complete five-command redraw. Once captured and with the source HUD frame
visible, copper palette pen 10 supplies its faded grayscale ink without
sampling a reticle pixel. The original pen-5 backdrop still gates contrast,
but its RGB now also comes from the displayed copper palette.
Until that redraw is known, or when its ink is no longer visible, the old
multi-pixel stencil gate remains active. Use
`FA18_HUD_RETICLE_CAPTURE=0` for the old gate alone, and
`FA18_HUD_RETICLE_CAPTURE_DEBUG=1` for capture/compositor diagnostics.
Keep both writer PCs in `tools/recomp_hooks.txt`. The new
`tests/check_gl_reticle_capture.sh` compares rendered flight/Pause frames
pixel-for-pixel with the legacy reticle visibility logic;
`tests/check_gl_reticle.sh` checks the native reticle's changed region.
`FA18_HUD_POINT_PALETTE_REFERENCE=1` restores the earlier source-pixel ink
lookup for both heading and reticle captures; the four-scenario
`tests/check_gl_hud_point_palette.sh` checks identical images and emulated
RAM hashes across buffer flips, flight, Pause and changing controls.

### Original HUD frame outline commands

A watch on the original HUD crown's bitplane words showed writes and clears
through the generic `$44856`/`$44a98` raster paths. The distinctive caller is
**`$48eba`**: it dispatches five ordered line draws to `$44686`, with
`(D0,D1)` as the start and `(D2,D3)` as the end:

1. `(85,144)` to `(85,45)` — left rail
2. `(85,45)` to `(93,36)` — left chamfer
3. `(93,36)` to `(225,36)` — crown
4. `(225,36)` to `(233,45)` — right chamfer
5. `(233,45)` to `(233,144)` — right rail

The matching `A0` values are consecutive entries beginning at `$4917c`.
`A2` identifies the current screen's four-entry bitplane table, descending
in 8,000-byte increments. The last entry identifies bitplane zero and the
existing per-bank instrument capture key. The hook in
`cockpit_instruments.cpp` verifies all five original segments **in order**
before latching an outline redraw for that bank; this avoids false positives
from the generic blitter and from partial frame writes.

When the displayed bank has a complete recent outline, the native HUD checks
the foreground crown, chamfer and rail colours from live copper pens
**10, 8 and 0**, respectively, with pen **5** as the background contrast
check. The bitmap is no longer inspected for frame visibility or ink in
this settled-flight path. Transitional, absent and stale-bank captures keep
the original bitmap gate, so a native HUD cannot appear before the original
frame has been drawn. `FA18_HUD_FRAME_BITMAP_REFERENCE=1` forces the old
source-pixel gate for comparison; `FA18_HUD_FRAME_CAPTURE_DEBUG=1` reports
completed bank writes and the selected visibility path.

`tests/check_gl_hud_frame_capture.sh ORIGINAL.adf [fa18-executable]`
compares screenshots and emulated RAM hashes with the source-pixel reference
across bank flip, settled flight, Pause, dynamic controls and maneuvering,
and checks both bank captures and an active writer path in settled flight.

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
