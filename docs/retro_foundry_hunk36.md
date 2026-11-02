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
this port. Changes under `src/native/` require `tests/check_native.sh` to pass;
the original ADF is available to the user but not in this build environment.

### Original-ADF entry-probe result (2026-11-02)

The user ran the port with their 331,232-byte original ADF, `--native off
--interpret --hires 3`, and `FA18_REGS=44686,80,...`. The resulting 80 calls
span 17 frames (beginning at frame 1329) and establish that negative mode is
**common**, not a merely theoretical branch:

- **80/80** entries had mode `$FFFF` (negative).
- **72** calls used mask `$FF`; **8** used `$0F`. Both enable all four display
  planes as far as the observed low nibble is concerned.
- **26/80** entries had differing active plane bits between the current
  hi-res observer's colour and the independently reconstructed source's
  `$5AC4F` flags. The other 54 entries agreed.

| `$5AC4C` | `$5AC4E` | Calls | Active plane bits that disagree |
| --- | --- | ---: | --- |
| `$0001` | `$0000` | 9 | 0 |
| `$0008` | `$0001` | 9 | 0, 3 |
| `$100B` | `$0008` | 8 | 0, 1 |
| `$0001` / `$0008` | matching lower nibble | 54 | none |

This confirms that a different colour choice would be *observable* in the
port's hi-res/GL model. It does not yet prove that the user's particular
executable has the same line-emitter opcode as the independent reconstruction.
The full log (which includes local machine details) is deliberately not
included in this repository.

To reproduce and automatically summarize future runs:

1. Record `$5A9E0`, `$5AC4C`, `$5AC4E`, and `$5A9DF` at `$44686`:

   ```sh
   FA18_REGS=44686,500,5a9df:b,5a9e0:w,5ac4c:w,5ac4e:w \
     ./build/fa18 --native off --interpret --hires 3 /path/to/original.adf \
     2>&1 | tee /tmp/fa18-line-probe.log
   python3 tools/analyze_line_regs.py /tmp/fa18-line-probe.log
   ```

   The entry logger reads these fields directly from emulated RAM (byte/word/
   long, big endian), so diagnostic reads cannot alter custom-chip state.
2. Check the original opcode's branch and data operand at `$44782` (the
   per-plane submit) against the reference:

   ```sh
   python3 tools/verify_line_flags.py \
     "/path/to/FA-18 Interceptor (1988)(Electronic Arts).adf"
   ```

   The verifier extracts the `F-18 Interceptor` Hunk executable temporarily,
   relocates it using the port's existing `loadseg.py` layout, and checks for
   eight ordered `BTST.B #plane,($address).L` instructions: four referencing
   `$5A9E1` (nonnegative mode) and four referencing `$5AC4F` (negative mode).
   It does not store the executable or the ADF in the repo. `MATCH` means the
   byte operands match the independent reconstruction; `INCONCLUSIVE` reports
   missing instructions and any other likely RAM selectors for follow-up
   disassembly. It does **not** independently prove the surrounding branches
   have identical control flow.

   Then compare low-resolution plane pixels and hi-res coverage in the same
   frame.
3. Only after byte verification, change `line_ops()` to use the correct flags;
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
