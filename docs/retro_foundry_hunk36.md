# Independent Hunk-36 reconstruction: renderer and bitplane paths

The earlier comparison flagged `$44B50` as a possible disagreement: our
high-resolution renderer observes a 2D polygon fill there, whereas the
independent [retro-foundry source](https://github.com/retro-foundry/fa18interceptor/tree/a8fc677966ef224e449a76637a51792169849573/source_amiga/observed)
names its corresponding `$C2FF48` entry a *display-submission wrapper*.
Those descriptions are **compatible**. The lower-level trace does not assign
semantic ownership to its tuple list; our port identifies its caller and
screen-space polygon data.

## Matching code and state addresses

Computed with `tools/reference_addr.py`, not by subtracting a fixed relocation
bias. Both projects use the same original Hunk indices and offsets.

| Reference runtime | Port runtime | Meaning / evidence |
| --- | --- | --- |
| `$C2FF48` | `$44B50` | DMA enable + call to list worker; general fill entry in our call graph |
| `$C301F6` | `$44DFE` | Worker consuming count-prefixed 2D `(x,y)` tuples |
| `$C4B390` | `$60684` | The same count-prefixed screen polygon buffer (`n`, then `n` XY pairs) |
| `$C2FA7E` | `$44686` | Bresenham/blitter line preparation and submit |
| `$C2FB7A` | `$44782` | Conditional per-bitplane line submissions |
| `$C456E7` | `$5A9DF` | Active line plane mask (low byte of `$5A9DE`) |
| `$C2FD8C` | `$44994` | Four consecutive full-width bitplane blits |
| `$C456B6` | `$5A9AE` | Pointer to the current four-plane table |
| `$C45984` | `$5AC7C` | Visible view-row limit / blit size input |
| `$C458D2` | `$5ABCA` | Bit `$2000` gates the alternative display path |
| `$C4589B` | `$5AB93` | Selects third plane's clear/set mode |
| `$C4591C/20/24` | `$5AC14/18/1C` | Counters updated during the three later blitter waits |

This independently corroborates our `docs/horizon.md` and the capture hooks
in `src/native/hires.cpp`:

- The `$44B50 → $44DFE` path reads the same `$60684` polygon data that our
  `on_fill2d` observer captures. Its bounded reference path reduces XY bounds,
  then emits a blitter line; the complete fill process involves more than the
  entry wrapper. It is not an unrelated renderer operation.
- The reference `$C2FD8C` setup loads four plane pointers, adds 40 bytes (one
  320-pixel scanline) to each destination, and derives `BLTSIZE` as
  `(row_limit << 6) + 20`. This agrees with our `$44994` backdrop clear/set
  model; the original reference labels the operation by its visible cockpit
  destination rather than our horizon/ground interpretation.
- The line emitter reads exactly the `$5A9DF` mask consumed by our line
  capture. Bit *p* selects plane *p*, with the pointer table in reverse
  order (offsets `12, 8, 4, 0` for mask bits `0, 1, 2, 3`).

**Evidence boundaries:** reference Hunk-36 CODE addresses are classified
`verified` against *its* executable. Hunk-71 and Hunk-72 DATA addresses are
`mutated_or_unmapped`, so the correspondence is an inferred Hunk offset plus
an independently matching usage, not a byte-for-byte comparison of our ADF.
The reference and this port used different executable variants.

## Possible bug: negative-mode line colour

The byte-exact reference `submit_blitter_line_to_active_planes.asm` contains
this decision for **each** enabled plane:

| Port address | Role |
| --- | --- |
| `$5A9E0` | Signed 16-bit line mode; negative selects a different flag byte |
| `$5A9E1` | Positive-mode plane bits (low byte of `$5A9E0`) |
| `$5AC4F` | Negative-mode plane bits (low byte of `$5AC4E`) |

For mode `>= 0`, the emitter tests bit *p* at `$5A9E1`. For mode `< 0`, it
tests bit *p* at **`$5AC4F`**, then adds either `$00FA` (set) or `$000A`
(clear) to the prepared `BLTCON0` value. Our `line_ops()` in
`src/native/hires.cpp` instead uses **`rd16($5AC4C)`** for the negative-mode
case, following the colour word used by `poly_ops()`.

These are different fields: our variables table already defines
`w_5ac4c` and `w_5ac4e` separately. This suggests that some negative-mode
lines and points may receive the wrong colour in the high-resolution overlay
even while the original low-resolution Amiga blitter is correct.

**Do not change `src/native/hires.cpp` yet.** First compare the reference's
line-emitter instruction window to the original, patched executable used by
this port, then observe the two flags at actual line-hook calls. Changes under
`src/native/` require `tests/check_native.sh` to pass, and the original ADF is
not available in this environment. There is no evidence yet that the negative
branch is taken in ordinary gameplay.

Useful probes once the original ADF is available:

1. Record `$5A9E0`, `$5AC4C`, `$5AC4E`, and `$5A9DF` on entries to `$44686`
   while `--hires` and `--gl` are active, and count negative-mode calls:

   ```sh
   FA18_REGS=44686,80,5a9df:b,5a9e0:w,5ac4c:w,5ac4e:w \
     ./build/fa18 --native off --interpret --hires 3 /path/to/original.adf
   ```

   The entry logger reads these fields directly from emulated RAM (byte/word/
   long, big endian), so diagnostic reads cannot alter custom-chip state.
2. Check whether `$5AC4F` differs from `$5AC4C` on those calls, and compare
   low-resolution plane pixels and hi-res coverage in the same frame.
3. Verify the original opcode uses `$5AC4F` before changing `line_ops()`;
   retain the native equivalence gate and visual regression tests.

## Scan another reference source

The crosswalk now accepts an ASM file and lists its unique `$Cxxxxx`
operands, including DATA references (rather than only routine filenames):

```sh
python3 tools/reference_addr.py ../fa18interceptor-reference \
  --scan source_amiga/observed/submit_blitter_line_to_active_planes.asm
```

It reports source line numbers, Hunk offsets, our port addresses, and the
reference's `verified` / `mutated_or_unmapped` statuses. Semicolon comments
are ignored. Hardware-register addresses outside the reference `$Cxxxxx`
runtime area are intentionally not treated as Hunk references.
