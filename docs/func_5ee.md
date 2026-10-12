# Function `$5ee` — edge / line span helper

## Resolution

Same pattern as the `$c6` motion function: the absolute `jsr $5ee.L` is
a **relocatable reference to offset `$5EE` inside a CODE hunk**, not an
indirect jump through a low-memory vector.

| Item | Value |
|------|-------|
| Hunk payload | file `0x28B4C` (size 4644) |
| Offset in hunk | **`$5EE`** |
| File offset | `0x2913A` |
| Live address | **`$00C2E5D6`** (hunk base `$00C2DFE8`) |
| Call sites | 11× `jsr $5ee.L` across geometry/draw overlays |

Chip-RAM `$5EE` remains zero — there is no low-memory vector slot.
Earlier docs listing `$5ee` as "overlay-only vector" were partially
right about residency, but the mechanism is relocatable CODE, not a
runtime-patched function pointer.

## Calling context

From draw-mode / perspective paths (`docs/draw_modes.md`,
`docs/transform_3d.md`):

```
movem.w $3000, …     ; screen-space vertex pair into regs
jsr     $5ee.L       ; this function
; → blitter line / span draw
```

Registers on entry (from call-site analysis):

| Reg | Role |
|-----|------|
| d1 | x0 / scan position |
| d2 | limit |
| d3 | x1 / other endpoint |
| d0 | related coordinate |

## Body (on-disk)

```asm
$5EE  move.w  $354.L, a2          ; live $00C446BC — line state ptr
      move.w  $324.L, $326.L      ; live $00C4468C → $00C4468E (copy)
      cmp.w   d1, d3
      beq.s   exit_related        ; zero-length → skip
      bls.s   path_lo             ; d3 ≤ d1

      ; d3 > d1: draw left-to-right span
      move.w  d3, d5
      sub.w   d1, d5              ; dx
      subq.w  #1, d5
      addq.w  #1, d1
      cmp.w   d2, d1
      bgt.s   …
      move.w  d1, d1
      asl.w   #3, d1              ; ×8 (bitplane / offset scale)
      move.w  d1, d7
      add.w   d7, d7
      add.w   d7, d7
      add.w   d1, d7              ; further scale
      move.w  d2, d4
      sub.w   d0, d4
      move.w  d0, d6
      lsr.w   #3, d4
      add.w   d0, d7
      bra.s   draw
      rts                         ; +$3A

path_lo:
      ; symmetric path for d3 ≤ d1
      …
```

Short function (~60 bytes to first `rts`).  It normalises a horizontal
span, scales coordinates for bitplane addressing (×8 shifts), and
hands off to the actual pixel/blitter write path.

## On-disk symbols

| Static | Live (session) | Role |
|--------|----------------|------|
| `$0324` | `$00C4468C` | line state word (copied) |
| `$0326` | `$00C4468E` | line state word (destination) |
| `$0354` | `$00C446BC` | pointer / base loaded into a2 |

## Status

- [x] Hunk + file offset identified (`0x28B4C + $5EE`)
- [x] Live address confirmed (`$00C2E5D6`)
- [x] Distinguished from low-memory vector theory
- [x] Role: edge/span setup before blitter line draw
- [ ] Full path_lo + draw-tail decompilation
- [ ] Wire into C++ blitter_line_stub as span helper
