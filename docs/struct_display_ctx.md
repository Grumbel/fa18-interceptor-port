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
- [x] Find the writer of `$00C16E22` / `$00C16E26` (AllocMem + field+$22); `$00C16E1E` still open
- [x] Confirm copper / bitplane interpretation of `$00C055F8`

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

## Copper / bitplane block at `$00C055F8` (confirmed)

The pointer at struct_A+`$0C` targets a **display-register poke / copper-source table**.
Registers are stored as the low 16 bits of their absolute custom addresses
(`$DFFxxx` → `$Fxxx`), a common alternative to the copper’s native `$0xxx` form.

### Header

| Addr | Words | Notes |
|------|-------|-------|
| `$00C055F8` | `$0001 $FFFF` | copper-style wait / terminator flavour |
| `$00C055FC` | `$0000 $0000` | padding / unused |

### Colour palette (COLOR00–COLOR15)

| Reg word | Name | Observed value |
|----------|------|----------------|
| `$F180` | COLOR00 | `$0000` |
| `$F182` | COLOR01 | `$0800` |
| `$F184` | COLOR02 | `$0888` |
| `$F186` | COLOR03 | `$0111` |
| `$F188` | COLOR04 | `$0151` |
| `$F18A` | COLOR05 | `$0447` |
| `$F18C` | COLOR06 | `$0036` |
| `$F18E` | COLOR07 | `$0444` |
| `$F190` | COLOR08 | `$0555` |
| `$F192` | COLOR09 | `$0D92` |
| `$F194` | COLOR10 | `$0666` |
| `$F196` | COLOR11 | `$0030` |
| `$F198` | COLOR12 | `$0333` |
| `$F19A` | COLOR13 | `$0777` |
| `$F19C` | COLOR14 | `$0777` |
| `$F19E` | COLOR15 | `$0333` |

Values are valid 12-bit Amiga colours (`$0RGB`).

### Display window & bitplane control

| Reg word | Name | Value | Meaning |
|----------|------|-------|---------|
| `$F08E` | DIWSTRT | `$0581` | display window start |
| `$F090` | DIWSTOP | `$40C1` | display window stop |
| `$F092` | DDFSTRT | `$0038` | data-fetch start |
| `$F094` | DDFSTOP | `$00D0` | data-fetch stop |
| `$F100` | BPLCON0 | `$0200` / `$4200` | bitplane control (multiple writes) |
| `$F102` | BPLCON1 | (zero-ish) | scroll |
| `$F104` | BPLCON2 | `$0024` | priority |
| `$F108` | BPL1MOD | 0 | modulo |
| `$F10A` | BPL2MOD | 0 | modulo |

### Bitplane pointers (Chip RAM)

| Reg | Name | High / Low | Full address |
|-----|------|------------|--------------|
| `$F0E0`/`$F0E2` | BPL1PT | `$0001` / `$2BC0` | **`$00012BC0`** |
| `$F0E4`/`$F0E6` | BPL2PT | `$0001` / `$4B00` | **`$00014B00`** |
| `$F0E8`/`$F0EA` | BPL3PT | `$0001` / `$6A40` | **`$00016A40`** |
| `$F0EC`/`$F0EE` | BPL4PT | `$0001` / `$8980` | **`$00018980`** |

Four bitplanes in Chip RAM, stride ≈ `$1F40` (8000 bytes) between planes — consistent with a 320×200 (or 320×180) 4-bitplane playfield.

### Layout note

The table is **not** a packed copper list.  Register tags and values are interleaved with zero words (long-alignment or builder scratch).  A copper-list builder (or direct custom-reg writer) walks this block and emits either copper MOVEs or `move.w` to `$DFFxxx`.

### Status

- [x] `$00C055F8` confirmed as copper/bitplane display setup block
- [x] COLOR00–15 palette recovered
- [x] DIW/DDF/BPLCON and four BPLPT addresses recovered
- [ ] Locate the copper-list emitter / custom-reg writer that consumes the block
