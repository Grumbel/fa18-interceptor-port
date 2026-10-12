# Unattributed structures `$00C01550` / `$00C03D46`

Previously mis-identified as `$be`/`$c6` install targets.  The install
vars are numeric scales at `$00C44426`/`$2E`.  These structures remain
real and are referenced from a pointer table around `$00C16E14`.

## Pointer table (in-flight)

| Location | Points to | Notes |
|----------|-----------|-------|
| `$00C16E1E` | **`$00C01550`** | struct_A |
| `$00C16E26` | **`$00C03D46`** | struct_B |
| `$00C16D8A` | `$00C01550` | another ref to A |
| `$00C7FF3C` | `$00C01550` | third ref to A |

## struct_A — `$00C01550` (display / view context)

| Off | Value | Interpretation |
|-----|-------|----------------|
| +00 | 0 | |
| +04 | 0 | |
| +08 | `$00C16D82` | back-pointer into pointer-table region |
| +0C | `$00C055F8` | sub-structure (word stream: `$F180`, `$F182`… — custom-reg style) |
| +10 | `$00C056E2` | related block (odd address) |
| +14 | `$A400` | possible BPLCON0-class constant |
| +1C | `$27`, `$28` | 39, 40 — display window / line pair |
| +28 | `$00C01598` | self+`$48` |
| +2C | `$10` | 16 |
| +30 | `$00C01598` | same |
| +34 | `$08` | |
| +38 | `$08` | |
| +3C | `$39A` | 922 |
| +44 | `$00C05748` | further block |
| +48 | `$00C051D0` | further block |

Hypothesis: **viewport / bitplane context** for the active display mode.
Constants `$A400`, 39/40, 16 fit Amiga display plumbing.  Sub-block at
`$00C055F8` streams values resembling custom-register addresses
(`$F180`…).

## struct_B — `$00C03D46` (secondary context)

| Off | Value | Notes |
|-----|-------|-------|
| +00 | 0 | |
| +04 | `$00C05D10` | pointer |
| +0C | `$002C0081` | |
| +10 | `$40000000` | high bit flag / fixed-point |
| +1C | 1 | |
| +20 | `$8F` | 143 |
| +24 | `$14B1` | 5297 |
| +40.. | more `$00C0xxxx` pointers | |

Less display-like; may be a **scene / object-list context** or mode-
specific scratch.  Shares the “leading zeros + mid-structure pointers”
shape with struct_A but different field population.

## Relation to mode install

Table `$151a` selects numeric scales into `$00C44426`/`$2E`, not these
pointers.  Something else (possibly mode `$220` path, or a one-time
setup) fills the `$00C16E1E`/`$26` pointer slots.  Not yet found as a
`move.l #addr, $…` in the static image (relocatable).

## Status

- [x] Decoupled from `$be`/`$c6` install vars
- [x] Pointer-table refs located
- [x] struct_A: display/viewport hypothesis (BPLCON-class const, DIW-like)
- [x] struct_B: secondary context, under-specified
- [ ] Find the writer of `$00C16E1E` / `$00C16E26`
- [ ] Confirm copper / bitplane interpretation of `$00C055F8`

## Pointer-table writers (primary)

### AllocMem path → `$00C16E22`

```asm
; live $00C0CDF8 / $00C14922 (primary file 0x6F0 / 0x821A)
pea     <size_or_template>
jsr     $00C52880            ; AllocMem wrapper (ExecBase -$228)
addq.l  #8, sp
move.l  d0, $00C16E22        ; store allocated block pointer
```

`$00C52880` is a thin AllocMem:

```
move.l  ExecBase, a6         ; $00C06A9C
move.l  8(sp), a1            ; attributes
move.l  12(sp), d0           ; size
jsr     -$228(a6)            ; AllocMem
rts
```

In-flight: `$00C16E22` = **`$00C01E1E`** (allocated object).

### Field copy → `$00C16E26`

```asm
; live $00C14938
move.l  $00C16E22, a0
move.l  $22(a0), $00C16E26   ; object field +$22 → table slot
```

So `$00C16E26` (struct_B pointer slot) is filled from **offset +$22 of the
Allocated object**, not by a direct `lea` of `$00C03D46`.  The in-flight
value `$00C03D46` is whatever that field currently holds (or a later
overwrite).

### Summary

| Slot | Writer | Mechanism |
|------|--------|-----------|
| `$00C16E22` | primary `$00C0CDF8`, `$00C14922` | AllocMem result |
| `$00C16E26` | primary `$00C1493E` | copy from alloc’d object `+$22` |
| `$00C16E1E` | (not found as abs store) | may be field of another object or unaligned neighbour |
| `$00C16E2E` | primary `$00C149E4` | `move.l a0, $00C16E2E` after another setup |

The “pointer table” is a set of **globals holding AllocMem’d display /
scene objects**, populated during init/mode change — not the `$be`/`$c6`
install path.
