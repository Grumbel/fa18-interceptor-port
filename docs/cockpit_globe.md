# Moving cockpit globe: source geometry and original flight replay

The lower `pix/inst5` image contains the aircraft's small blue/red attitude
globe. It is **not** safe to mark the whole surrounding panel as static.
The ADF is the source of truth for its colours, boundaries and decorative
overlays; original artwork is neither copied nor generated into the repo.

## Confirmed from the supplied ADF

Run `python3 tests/check_globe_asset.py ORIGINAL.adf`. Its unpacked five-plane
image identifies two source indices in the globe:

| Pen | Original RGB | Count | Bounds (inclusive) |
| --- | --- | ---: | --- |
| 21 | `#404070` (blue) | 172 | x=198..220, y=163..172 |
| 17 | `#800000` (red) | 186 | x=198..220, y=173..183 |

The full coloured mask is 358 cells (not a filled rectangle), and the level
horizon crosses original screen row 173. Source indices outside these two
fills include the bezel, ticks and surrounding instrument markings. Those
must not be overwritten by a naive red/blue rectangle or a black-key mask.
The ILBM has **five** bitplanes, but the original cockpit flight uses a
**four-plane** display. As a result the ADF's 17 (red) and 21 (blue) become
live palette indices **1** and **5**, respectively. Their observed original
flight copper RGB values in the headless replay were `#880000` and `#444477`
on row 170; neither equals the original ILBM's CMAP entry. Always use the
active **copper row and displayed bank** for colours.

## Confirmed from a full original-code replay

There is now a no-SDL/no-GL way to run the original 68000 interpreter and
inspect its actual double-buffered cockpit output:

```sh
bash tools/run_globe_writer_probe.sh ORIGINAL.adf \
  tests/data/cockpit_radar_maneuver.txt 4700 100
# Also check source-art stencil, live pen remapping, moving horizons, and
# original BLTSIZE writer attribution in both original screen banks:
bash tests/check_globe_writer_probe.sh ORIGINAL.adf
```

The instrument's fill is dynamically recoloured **inside the same ADF
silhouette**, preserving its ticks, bezel and uncoloured marks. With the
script above, the interpreter displays the following poses:

| Frame | Player record +102 (wrapped angle) | First red row | Compared with source |
| ---: | ---: | ---: | --- |
| 3500 | 0 | 173 | all 358 original fill cells match |
| 4500 | 25376 (−42.8°) | 182 | boundary offset +9 exactly matches all 358 |
| 4700 | 2072 (+25.9°) | 166 | boundary offset −7 exactly matches all 358 |

The displaced red/blue boundary is horizontal at those three points. The
pixel comparison also confirms that all *non-fill* cells inside the 23×21
bounds are unchanged by the manoeuvre. Intermediate flight includes bank
inversions (`+106 = 14400`), so the horizontal split is not a general
replacement for full roll geometry.

A second replay (`tests/data/cockpit_globe_roll.txt`) holds pitch at zero and
rolls the aircraft left. At frame 4480 its +106 word is 1440 (18° in game
units), and the observed fill fits a screen-space rotation of **−8°** with
an offset of **+0.65 source pixels**. At frames 4500 and 4700 the +106 word
is 2216 (27.7°); both masks fit **−20° rotation and +1 source-pixel
offset**. All 358 fill cells match at all three measured rolled poses.
The offsets and rotations are *measured display geometry*, not a verified
conversion from the game's bank field. The regression script checks both
flight manoeuvres and preserves the original non-fill marks.

An independent **ground-turn negative control**
(`tests/data/cockpit_globe_ground_turn.txt`) keeps the player on the runway
while steering left. At frame 4150 the player's heading is 28.0° and bank is
0°; by frame 4360 heading has wrapped to 301.3° while bank is only 2.0°.
The globe remains **horizontal** throughout, with its level fill boundary
moving from row 173 to 174 and then back to 173 at frame 4440. The full
358-cell stencil remains intact, no camera-view sequence is active, and
pitch remains zero. This confirms that a large **heading** change is not
enough to roll the instrument. The one-row boundary difference is observed
at the original raster resolution; it does not establish a universal
degrees-to-pixels factor. The regression now replays and checks all four
ground-turn checkpoints in addition to both airborne/landing manoeuvres.

To fit additional poses from original pixel observations (the result prints
both the selected angle and the range of exact pixel fits), run:

```sh
FA18_GLOBE_MAP_FRAMES=3500,4440,4480,4500,4700 \
  bash tools/run_globe_writer_probe.sh ORIGINAL.adf \
    tests/data/cockpit_globe_roll.txt 4750 20 > /tmp/globe-roll.log
python3 tools/fit_globe_boundary.py ORIGINAL.adf /tmp/globe-roll.log
```

The fit is quantised by the original 320×200 screen. Intermediate values
change more slowly than the player's bank word, and the exact-fit angle
ranges overlap multiple nearby orientations. Neither these static samples
nor the best-fit angle establish the original update latency or formula.

A denser capture every four frames from **4400–4520** confirms that the
boundary advances in visible steps while the aircraft bank field changes
more frequently: the observed angle is approximately `-3.5°` at frame
4476 (bank `12.4°`), `-8°` at 4480 (`18.0°`), `-12°` at 4484 (`23.2°`),
`-16°` at 4488 (`25.7°`), `-17.5°` at 4492 (`26.9°`), and `-19.5°`
at 4496 (`27.4°`). These are fitted displayed boundaries, **not** a
validated input-output conversion.

The same capture exposes a subtlety missed by the static geometry model:
the **whole instrument panel moves vertically** at frames 4488 and 4492,
including the globe's 358-cell fill stencil. Relative to the original
`pix/inst5` stencil, the displayed globe's Y shift
is respectively **-1** and **+1** screen pixel. The 21-row capture crops
seven cells at the edge, leaving exactly **351 visible coloured cells**
at each pose. After translation, both maps match the original stencil
with zero missing or extra cells; their tilted red/blue boundaries also
fit with zero classification errors. These cells were initially mistaken
for foreground occlusions. The boundary fitter now detects and reports
the stencil shift explicitly. A wider original-screen measurement proves
that this is **not globe-specific geometry**: against frame 4484, 910
non-globe cockpit pixels match at frame 4488 after a -1 pixel shift with
only two differences (versus 321 differences without shifting). Frame
4492 matches after +1 pixel with only two differences (versus 327); by
4496 all 910 pixels again match with no shift or differences. Both
observations are checked in the headless replay regression. A second
control replay with the **same flight-entry commands but no roll input**
shows the **same -1/+1/0 displacement** at those frames (with only two,
two and zero mismatches). Its pitch and bank fields remain zero.

**The trigger is now identified: first ground contact, not a compressor
stall.** In the no-roll control replay the player's altitude falls from
1404 at frame 4476 to 816 at 4480, then reaches the 750-unit ground floor
at 4484. The player's state word at +2 changes from `$2000` to `$2080`,
setting the on-ground `$80` bit. The original landing path in `$26b16`
selects `$5aab8=1`; `$39b06` advances that scripted camera-view mode.
The mode's sequence at frames 4484/4488/4492/4496 is:

| Frame | Mode/tick | View `$5ac7c` | Forward `$5ac10` | Altitude |
| --- | --- | --- | --- | --- |
| 4484 | 1/1 | 143 | -40 | 750 |
| 4488 | 1/2 | 145 | +40 | 750 |
| 4492 | 1/3 | 144 | 0 | 750 |
| 4496 | 1/4 | 144 | 0 | 750 |

The **original executable** contains a four-entry view-sequence table at
`$39ade`: modes **1 and 3** both have signed steps `[-1,+1,0,0]`, while
modes 2 and 4 use `[+1,-1,0,0]`. `$39b06` scales a step by 40 bytes and
writes it to `$5ac10`, which the cockpit-copy path `$44c42` consumes.
The same animation can therefore accompany other events: the native
translation of the original engine-speed path selects mode 3 above its
8400-unit threshold. That explains the visual similarity to an
afterburner/overspeed incident. In *this* replay the engine-speed flag
`$5abca` is zero and the speed is only 867 at touchdown. The precise
one-bank redraw/scanout latency of the panel movement still needs tracing.
Do not bake the shake into the globe-angle formula; the native cockpit's
panel presentation must reproduce it separately.

The strongest live blitter sites attributed to this exact globe region are
`$44d08` (plane 2, minterm `$22`) and `$451ac` (plane 2, minterm `$EC`),
repeated from approximately frame 3478 in **both** original screen banks
`$06a098` and `$0a5a18`. These are low-level blitter starts, not yet a
proven high-level attitude-control routine. The earlier `$45350` bulk copy
seen on frame 377 was not representative of the moving writer.

The next read-only call-stack trace identifies a higher-level **blitter
rectangle routine**, not merely its final `BLTSIZE` writes. The original
flight-loop call at `$213a8` enters `$44c42` (return address `$213ae`).
Inside it, `$44c80` invokes shared clipping helper `$45ce6`, then the
`$44d08` area blit invokes `$44da4` before reaching `$451ac` with stack
returns `$44df2`, `$44d10`, `$213ae`. In the bank-only replay the sequence
recurs at frames **4472, 4476, 4480, 4484 and 4488**. Its `$44d08` entry
uses `BLTSIZE=$0542` (21 rows × 2 words), and alternates A2 source pointers
`$5a976`, `$5a966`, `$5a976`, `$5a966`, `$5a976` with the displayed banks.
The actual scanout row key at frame 4480 is `$06bb28` (first bank
`$06a098` + `$1a90`), and at frame 4500 it is `$0a74a8` (second bank
`$0a5a18` + `$1a90`). This gives a concrete per-bank redraw cadence and a
viable capture entry.
It does *not* identify the upstream roll/pitch-to-source-data calculation.

### Separating the panel reset from the moving-boundary draw

An additional read-only source-bank probe now compares the original **four
bitplanes** at x=192..223, y=163..183 before every high-level cockpit
routine in the 68000 game loop. It follows both buffer bases directly from
the `$5a966` and `$5a976` pointer tables, so it does not conflate the
displayed framebuffer with the current drawing bank. In the rolled replay:

| Frame | Updated bank | After `$45368`, before `$455ba` | After `$44c42`, before `$474ac` |
| ---: | --- | ---: | ---: |
| 4480 | `$06a098` | 268 changed bits | 33 changed bits |
| 4484 | `$0a5a18` | 282 changed bits | 35 changed bits |

In the corresponding **zero-pitch, zero-bank** replay, the first operation
still changes 254/270 bits, whereas the second changes only **one bit** at
each frame. Consequently `$45368` performs a substantial base-panel rewrite
regardless of roll, while the later `$44c42` pass is responsible for the
extra angle-sensitive image changes. `$45368`'s four Amiga blitter starts
appear within instructions `$453fc`–`$4540c`, before the other cockpit
helpers run. Neither observation establishes the arithmetic that converts
aircraft angles into the final boundary: `$44c42` remains a generic
rectangle/blitter path with precomputed inputs.

### Independent retro-foundry cross-reference

The [retro-foundry 68000 reconstruction](https://github.com/retro-foundry/fa18interceptor/)
contains the original **parent-call schedule** for both routines. Translate
its Engine9000 live addresses by Hunk offset, not a global relocation bias:

| This port | retro-foundry | Original Hunk | Reference call context |
| --- | --- | --- | --- |
| `$213a8` | `$C0F250` | 0 + `$13A0` | `run_parent_activity_stages.asm` calls `$C3003A` |
| `$44c42` | `$C3003A` | 36 + `$BAA` | `ACTIVITY_HELPER_4`, among thirteen activity-stage helpers |
| `$45368` | `$C30764` | 37 + `$A4` | `run_parent_postflight_setup.asm` calls the first postflight helper |
| `$44d08` | `$C30100` | 36 + `$C70` | The active globe blitter path in our original-ADF trace |
| `$451ac` | `$C305A4` | 36 + `$1114` | The later plane-2 blitter submit in our original-ADF trace |

Verified mapping command with an upstream checkout beside this repository:

```sh
python3 tools/reference_addr.py ../fa18interceptor-reference \
  --from-port 0x213a8 0x44c42 0x45368 0x44d08 0x451ac
```

At upstream commit `a8fc677966ef224e449a76637a51792169849573`, these
are **call-site labels**, not high-level reconstructed attitude functions.
The reference's Hunk-37 `$C30764` path is also seen in its attract-mode
instruction trace, whereas the activity-stage `$C3003A` call is represented
in its reconstructed parent sequence. Our separate headless original-game
replay establishes the panel/globe data changes; those meanings must not be
attributed to the upstream symbolic labels as though they were independently
proved there.

The same writer-entry probe resolves the apparent touchdown timing offset.
At **game-code** frame 4480, `$44c42` already sees `$5ac10=-40`; at frame
4484 it sees `+40`, then `0` at 4488. In **displayed** frames 4488, 4492,
4496, the entire panel respectively moves `-1`, `+1`, `0` source rows. The
displayed displacement thus trails its blitter input by two four-VBL redraw
intervals in this scripted flight. This is a measured two-bank presentation
schedule, not part of the globe's roll formula. The previously recorded
`globe-view-sequence` lines show state at the *host's frame callback* and
therefore lag the in-frame 68000 writer-entry trace.

The rectangle-entry registers make the shift explicit: `$44c42` receives
`D0=112` and `D2=2` throughout, while its `D1` destination Y moves
**158 → 157 → 159 → 158** at code frames 4476/4480/4484/4488. That is
exactly the signed forward offset divided by the 40-byte bitplane stride,
and it is identical with or without roll. The verified `BLTSIZE=$0542`
draws 21 rows × 2 words. A native replacement should apply this rectangle
translation independently of the orientation-dependent red/blue split.

To reproduce the non-mutating call-boundary comparison:

```sh
FA18_GLOBE_STAGE_TRACE=4472:4500:60 \
  bash tools/run_globe_writer_probe.sh ORIGINAL.adf \
    tests/data/cockpit_globe_roll.txt 4504 100
# Optional, more verbose: dump source-bank R/B masks after each pass.
FA18_GLOBE_STAGE_TRACE=4480:4480:30 FA18_GLOBE_STAGE_MAPS=1 \
  bash tools/run_globe_writer_probe.sh ORIGINAL.adf \
    tests/data/cockpit_globe_roll.txt 4484 100
# For actual 68000 instruction boundaries within the panel reset:
FA18_GLOBE_STAGE_TRACE=4480:4480:60 FA18_GLOBE_STAGE_INSNS=1 \
  bash tools/run_globe_writer_probe.sh ORIGINAL.adf \
    tests/data/cockpit_globe_roll.txt 4484 100
```

The automated check now compares the roll and no-roll bitplane deltas at
these call boundaries and verifies the forward-offset-to-visible-shake
latency. The trace reads chip RAM and never changes the guest drawing.

The original **bank-selection code is now identified separately from the
instrument pattern producer**. A RAM-write watch shows `$283d2` toggling
word `$5a964` between 0 and 1 once per four frames. The selector routine
at `$44160` reads it; `$4417c` writes either `$5a966` or `$5a976` into
`$5a9ae`, while `$44182` writes either `$5a986` or `$5a99a` into `$5a9b2`.
`$44c42` later reads the selected descriptors for its drawing pass. On the
five tested frames, the selector values are `1, 0, 1, 0, 1`; corresponding
descriptor pairs are `(5a976,5a99a)` and `(5a966,5a986)`. These pointers
select the **buffer bank**, not the aircraft bank angle or a precomputed
attitude image. The replay regression verifies all 15 writes directly.

To inspect original call registers and stack without an instruction-wide
trace, set `FA18_GLOBE_WRITER_REGS=4470:4490:80` on the headless runner.
The probe filters the shared clipping helper to the globe caller and never
writes to guest memory.
Use `FA18_GLOBE_SOURCE_WATCH=4420:4500:128` to trace CPU/HLE writes to the
adjacent source-bank descriptor range `$5a960`–`$5a9cf`. This read-only bus
observer records the original instruction PC, address, width and new bytes.
Use `FA18_GLOBE_PANEL_ANCHOR=4484` with
`FA18_GLOBE_MAP_FRAMES=4484,4488,4492,4496` to compare the 910 non-globe
pixels near the instrument to a fixed source frame, checking row shifts
from -2 to +2. The probe reports aligned and unaligned difference counts;
it does not alter the game or the rendered frame.
The same explicit map frames also print `globe-view-sequence` with the
original event mode, sequence step, camera offsets, ground-state bit,
altitude and speed, all read directly from interpreter RAM.

## Reusable geometry, not yet wired

`src/cockpit/globe_model.hpp` is a pure C++ model of the moving colour
boundary. It takes a signed vertical offset in source pixels and an angle in
the game's 28,800-units-per-revolution representation (80 units/degree).
It exposes a continuous signed boundary distance and coverage for anti-aliased
native scaling. Only original **ADF** fill cells with indices 17 and 21 may
be recoloured; any other pen and any source cell outside the globe's tight
bounds passes through unchanged. Its `palette_index` function is a
source-resolution reference. `flight_palette_index` converts the fill to
the live four-plane palette indices 1 and 5. `sky_coverage` is suitable
for rendering smooth edges at arbitrary OpenGL output scales.

The geometry's 0-degree, 0-offset split agrees with all 358 fill cells in
the ADF level image **and the running interpreter at frame 3500**.
`tests/globe_model.cpp` verifies angle wrapping, bank rotation, all three
observed horizontal boundary locations, two samples from the measured
rolled pose, AA coverage and bezel/mark exclusion. The pure geometry model
deliberately does not implement the **whole-panel** movement at 4488/4492.

To view an experimental pose outside the game (no SDL/Nix required), run:

```sh
python3 tools/preview_globe.py ORIGINAL.adf /tmp/globe.png \
  --bank 25 --offset 1.5 --scale 4
```

This produces an enlarged PNG using the original ADF artwork. Its bezel,
ticks, lettering and non-coloured cells remain untouched. At bank=0 and
offset=0, the 1x output is **byte-identical** to the original asset; at a
nonzero angle, the changed cells stay within the ADF's red/blue fill mask.
The high-resolution boundary uses one-output-pixel coverage; this preview
cannot establish the original instrument's pitch/bank mapping or timing.

**Not yet established:** the exact relationship between the original
high-level instrument routine and the aircraft's actual pitch/bank values,
its vertical scale in pixels per degree, roll sign and source geometry
generation before each four-frame redraw. In the recorded manoeuvre the
player word at +102 varies alongside vertical movement and +106 reaches
14400 during inversion, but this correlation does **not** prove the
instrument's input formula or the precise displayed-bank latch semantics.
See `src/native/view.cpp` for the player record's attitude usage. Do not
hardwire those candidate fields or the level-only displacement into the GPU
compositor until intermediate writer frames and inverted attitude have been
checked.

## Integrating the replacement

1. Use the headless scripted replay and verified `$44c42` routine entry and
   `$44d08`/`$451ac` writer sites to capture complete per-bank globe updates.
   Treat `$44160`/`$283d2` as the screen-bank selector only; trace the actual
   attitude-image producer before the `$213a8` call and compare it
   against +102/+106 for pitch/roll, including inverted flight and frames
   between controls and their visibly updated boundary.
2. Compare normal and `--hide-amiga-region globe` screenshots through
   `tests/check_gl_scanout_region.sh ORIGINAL.adf globe`.
3. For the active bank, require a verified cockpit-frame latch and complete
   attitude update, then feed the ADF fill stencil and captured orientation
   into a native GL layer. Preserve the bezel and other original foreground
   until their own writer captures are complete. Keep the original panel
   movement distinct from the globe's changing red/blue horizon; use pens
   1/5 from the
   **active copper row** instead of hardcoded RGB colours.
4. Check flight/Pause transitions, bank flips, GL-direct vs CPU output,
   moving-angle source bounds and unchanged RAM hashes. Run the complete
   `tests/check_native.sh` gate before committing changes in `src/native/`.

The current model and its tests are an independently verified building block,
**not** a claim that the moving instrument has already been replaced.
