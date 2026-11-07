# Moving cockpit globe: first native geometry model

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
The live copper palette still controls the actual on-screen colour for pens
17 and 21; the ILBM CMAP is only an asset-level reference.

## Reusable geometry, not yet wired

`src/cockpit/globe_model.hpp` is a pure C++ model of the moving colour
boundary. It takes a signed vertical offset in source pixels and an angle in
the game's 28,800-units-per-revolution representation (80 units/degree).
It exposes a continuous signed boundary distance and coverage for anti-aliased
native scaling. Only original **ADF** fill cells with indices 17 and 21 may
be recoloured; any other pen and any source cell outside the globe's tight
bounds passes through unchanged. Its `palette_index` function is a
source-resolution reference, while `sky_coverage` is suitable for rendering
smooth edges at arbitrary OpenGL output scales.

The geometry's 0-degree, 0-offset split agrees with all 358 fill cells in
the ADF level image. `tests/globe_model.cpp` verifies angle wrapping,
bank rotation, pitch-offset direction, AA coverage and bezel/mark exclusion.

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

**Not yet established:** the relationship between the original instrument
writer and the aircraft's actual pitch/bank values, its vertical scale in
pixels per degree, roll sign and update timing across the two screen banks.
The player aircraft record contains candidate attitude words at +102,+104,
+106 (see `src/native/view.cpp`), but **no verified claim** is made that a
particular word drives each part of this instrument. Likewise, the observed
`BLTSIZE` write at PC `$45350` was a bulk screen-copy operation, not proof
of the dynamic globe writer. This model is deliberately not used at runtime
until those relationships can be checked against a scripted flight.

## Integrating the replacement

1. Run the ADF-backed flight with `FA18_COCKPIT_CPU_TRACE=1` and
   `FA18_COCKPIT_BLIT_TRACE=1`; distinguish moving writes from initial art
   copies and track the displayed bank for every update.
2. Compare normal and `--hide-amiga-region globe` screenshots through
   `tests/check_gl_scanout_region.sh ORIGINAL.adf globe`.
3. For the active bank, require a verified cockpit-frame latch and complete
   attitude update, then feed the ADF fill stencil and captured orientation
   into a native GL layer. Preserve the bezel and other original foreground
   until their own writer captures are complete. Use pens 17/21 from the
   **active copper row** instead of hardcoded RGB colours.
4. Check flight/Pause transitions, bank flips, GL-direct vs CPU output,
   moving-angle source bounds and unchanged RAM hashes. Run the complete
   `tests/check_native.sh` gate before committing changes in `src/native/`.

The current model and its tests are an independently verified building block,
**not** a claim that the moving instrument has already been replaced.
