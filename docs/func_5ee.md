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
      beq.s   <back>              ; zero-length → skip
      bls.s   path_lo             ; d3 ≤ d1

; --- path_hi (d3 > d1): left → right ---
      move.w  d3, d5
      sub.w   d1, d5              ; dx = x1 - x0
      subq.w  #1, d5
      addq.w  #1, d1              ; x0++
      cmp.w   d2, d1              ; past limit?
      bgt.s   rts
      asl.w   #3, d1              ; bitplane scale ×8
      move.w  d1, d7
      add.w   d7, d7
      add.w   d7, d7
      add.w   d1, d7              ; d7 = x×40 (8+32)
      move.w  d2, d4
      sub.w   d0, d4
      move.w  d0, d6
      lsr.w   #3, d4
      add.w   d0, d7
      bra.s   draw_tail
      rts

; --- path_lo (d3 ≤ d1): right → left ---
path_lo:
      move.w  d1, d5
      sub.w   d3, d5              ; dx = x0 - x1
      subq.w  #1, d5
      move.w  d0, d4
      sub.w   d2, d4
      addq.w  #1, d3
      cmp.w   d2, d3
      bgt.s   rts                 ; 6EEE → +$3A
      move.w  d3, d1
      asl.w   #3, d3
      move.w  d3, d7
      add.w   d7, d7
      add.w   d7, d7
      add.w   d3, d7              ; same ×40 scale
      move.w  d2, d6
      lsr.w   #3, d2
      add.w   d2, d7
      ; fall into draw_tail

; --- common draw tail ---
draw_tail:
      ext.l   d7
      moveq   #1, d1
      andi.w  #$000F, d6          ; bit offset within word
      lsl.w   d6, d1              ; bit mask
      addi.w  #$0B00, d6          ; BLTCON-style adjust
      tst.w   d4
      bmi.s   …
      cmp.w   d5, d4
      bcs.s   …
      addi.w  #$10, d1
      ; … blitter register setup continues …
```

Symmetric paths normalise endpoint order, reject zero-length and
out-of-limit spans, scale X by 40 for Amiga bitplane row offsets,
then build a bit mask for the blitter.

## C++ port

`span_setup()` in `src/draw.hpp` captures the normalise / reject /
order logic.  Bitplane scaling and BLTCON setup remain stubbed until
a real planar backend exists.

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
- [x] Full path_hi / path_lo / draw_tail decompiled
- [x] C++ `span_setup` in draw.hpp
