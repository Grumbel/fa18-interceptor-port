# Blitter Hunk 35 (file payload 0x28274, size 2068)

## Entry stubs

```
+0000  move.w #$1b, $36e     ; mode / state variable
       jsr    $12            ; low-memory vector
       moveq  #0, d0
       rts

+0012  moveq  #0, d0
       move.l #-1, $328
       rts
```

Small setup helpers that write low-memory state and call vector `$12`.

## Coordinate / viewport math (+0x20 onward)

Code performs bound checks and multiplications consistent with a
**320×180** (or similar) viewport:

| Constant | Hex | Likely meaning |
|----------|-----|----------------|
| 160 | `$a0` | half-width / centre X |
| 320 | `$140` | screen width |
| 90 | `$5a` | half-height / centre Y |
| 180 | `$b4` | viewport height |

Typical pattern:

```
muls / divs involving $a0, $5a
cmpi  #320, d0
cmpi  #180, d1
branch if out of range
```

This is the classic “clamp projected vertex to screen” preamble used
before blitter line/fill operations in Amiga 3D engines.

## Function density

~14 `rts` sites in 2068 bytes → many small primitives (set mode, clamp,
draw span, wait blitter, etc.).  Register-based arguments (no `link a6`).

## Relationship to $dff000 sites

Later in this hunk and in hunk 36 the code does:

```
lea  $dff000, a0/a2
btst #6, 2(a0)          ; blitter busy
… blitter register setup …
```

Hunk 35 appears to own **geometry prep + clamp**; hunk 36 owns more of
the actual blitter register programming.

## Port note

These primitives map cleanly onto SDL2 software/hardware line and
rectangle draws.  Implementing screen-space clamp + draw-line stubs
will let the higher-level 3D code (once identified) produce visible
output without a cycle-accurate blitter.

## Blitter register setup (hunk 35/36 boundary)

### Wait loop (file 0x288c8)

```
lea   $dff000, a2
btst  #6, 2(a2)        ; DMACONR bit 14–6 = BBUSY
beq   ready
nop
nop
bra   wait
move.w #0,      $42(a2) ; BLTCON1 = 0
move.w #$ffff,  $74(a2) ; BLTADAT = $ffff (for line fill)
```

### Modulo setup (file 0x29222, hunk 36 area)

```
move.w #$28, $66(a0)   ; BLTDMOD = 40
move.w #$28, $60(a0)   ; BLTCMOD = 40
```

**40 bytes = 320 pixels** at 1 bitplane depth. Confirms the playfield
width used by the blitter is 320.

### Hunk 36 entry stubs (file 0x28b4c)

```
move.l #$fffff, $b6    ; mask / plane enable?
rts
move.l #$fffff, $b6
move.w #$d, $324       ; mode word
… load d0–d3 with small constants (0,0,8,9) or (8,0,0,9)
```

Likely plane-mask and draw-mode selectors invoked before the heavier
blitter sequences.

## Low-memory state used by blitter path

See `docs/lowmem_vars.md`:

| Addr | Role |
|------|------|
| `$36e` | draw mode ID |
| `$324` | pen / plane select |
| `$b6`  | bitplane enable mask |
| `$328` | sentinel |

## Line / fill blitter entry points (hunk 36 region)

Several complete blitter setup + wait sequences live near file **0x29a80–0x29b80**.

### Candidate line/fill setup A (file 0x29a88)

```
lea   $dff000, a0
btst  #6, 2(a0)          ; wait BBUSY
… nop loop …
move.w #$09f0, $40(a0)   ; BLTCON0
move.w #$000a, $42(a0)   ; BLTCON1
move.l d2, $50(a0)       ; BLTAPTL?
move.l d2, $54(a0)
move.l d0, $44(a0)       ; BLTCPTL?
move.w d3, $64(a0)       ; BLTAMOD
move.w d3, $62(a0)
move.w d3, $66(a0)       ; BLTDMOD
move.w d7, $58(a0)       ; BLTSIZE → start
rts
```

BLTCON0 = `$09f0` is a typical value used for single-plane line or
area-fill operations (ASHIFT + USEA/USEC/USED bits).

### Candidate B (file 0x29aea)

```
lea   $dff000, a0
… wait …
move.w d4, $40(a0)       ; BLTCON0 from register
move.w #$0002, $42(a0)
… pointer / modulo setup …
rts
```

### Candidate C (file 0x29b66)

```
move.w #$0dfc, $40(a0)   ; BLTCON0
```

Other observed BLTCON0 immediates in the region: `$0d0c`, `$0d3c`,
`$0fec`, `$0f4c`.  These cover the family of line and fill modes used
by the engine.

### Port mapping

```cpp
// Approximate equivalents
void blitter_line(int x0, int y0, int x1, int y1, uint8_t pen);
void blitter_fill(int x, int y, int w, int h, uint8_t pen);
// or SDL_RenderDrawLine / filled rect after the 320×180 clamp
```

The exact register-to-argument mapping (which Dn holds start/end
coordinates) still needs a few more traces, but the entry points and
BLTCON0 values are now identified.

## Register / argument mapping (file 0x29a00–0x29ac6)

Coordinate prep ends with:

```
move.l d1, $338
move.l d1, $330
move.l d1, $334
move.w d7, $33e
```

Then the blitter is programmed (a0 = $dff000):

| Register | Blitter target | Offset |
|----------|----------------|--------|
| d2       | BLTAPT / BLTBPT | $50 / $54 |
| d0       | BLTCPT         | $44 |
| d3       | BLTAMOD / BLTCMOD / BLTDMOD | $64 / $62 / $66 |
| d7       | BLTSIZE        | $58 |

BLTCON0 is hard-coded `$09f0`, BLTCON1 `$000a`.

Exact mapping from geometric endpoints (x0,y0,x1,y1) onto d0–d3/d7
is still being refined; the intermediate stores to `$330`–`$33e` are
the best static anchors for a later dynamic trace.

### C++ status

`src/draw.hpp` now exposes `draw_line`, `draw_fill` and
`blitter_line_stub` that mirror the clamp + mode write.  A simple
perspective runway is rendered in the Flight state as a stand-in for
true 3D projector output.

## Line-parameter geometry (file 0x29a14 → blitter)

### Inputs from low-memory

| Addr | Role (inferred) |
|------|-----------------|
| `$350` | (paired with $352; exact role open) |
| `$352` | **Y coordinate** (row index) |

### Row-bytes conversion

```
move.w $352, d5
move.w d5, d1
asl.w  #3, d1          ; ×8
move.w d1, d0
add.w  d0, d0
add.w  d0, d0          ; ×32
add.w  d0, d1          ; d1 = Y × 40
add.w  d2, d2
ext.l  d2
add.l  d2, d1          ; d1 += X-related byte offset
```

**Y × 40** is the classic Amiga conversion for a 320-pixel-wide
bitplane (40 bytes/row).  Confirms playfield width = 320.

### Blitter register mapping (line mode)

| Register | Source | Notes |
|----------|--------|-------|
| BLTCON0 | `#$09f0` | A/C/D enable, minterm `$F0` |
| BLTCON1 | `#$000a` | line-mode control bits |
| BLTAPT  | d2 | pattern / texture pointer |
| BLTBPT  | d2 | same |
| BLTCPT  | d0 | destination start address |
| BLTAMOD | d3 | modulo (often `$28` = 40) |
| BLTBMOD | d3 | |
| BLTDMOD | d3 | |
| BLTSIZE | d7 | encoded start + height |

### Scratch mirrors

| Addr | Content |
|------|---------|
| `$330` | computed address (d1 after arithmetic) |
| `$334` | copy of same |
| `$338` | copy of same |
| `$33e` | BLTSIZE value (d7) |

### Geometric summary

- **d5 / `$352`**: screen Y
- **d1 after ×40**: byte offset of row in bitplane
- **d2**: contributes X byte offset; also used as BLTAPT pattern pointer
- **d0**: final destination address → BLTCPT
- **d3**: bitplane modulo (40)
- **d7**: BLTSIZE (line length / octant encoding)

Exact octant/BLTCON1 bit encoding and the role of `$350` remain open.

## Line endpoints and BLTSIZE encoding

### Endpoint Y coordinates

| Addr | Role |
|------|------|
| `$350` | **Y0** – start (or first) endpoint Y |
| `$352` | **Y1** – end (or second) endpoint Y |

```
move.w $352, d5          ; Y1
… Y1 × 40 → row byte offset …
sub.w  $350, d5          ; d5 = Y1 − Y0
addq.w #1, d5            ; d5 = dy + 1  (inclusive height)
move.w d5, d7
addq.w #1, d6            ; d6 holds a width-related term
sub.w  d0, d7
lsl.w  #6, d7            ; height << 6
add.w  d6, d7            ; BLTSIZE = (height << 6) | width_term
move.w d7, $33e
```

Standard Amiga BLTSIZE packs `Height` in bits 15–6 and `Width` in
bits 5–0.  The `lsl #6` matches that layout exactly: the Y-delta
(+1) is the blitter height component.

### Clip bound

`a4` is compared against Y (`cmp.w a4, d7`) and used to subtract a
clipping adjustment when the line extends past the bound.  Likely
holds the viewport bottom (or top) edge.

### Bitplane base

`$b2` is added to the computed row address before it is stored to
`$330`/`$334` and used as BLTCPT:

```
add.l $b2, d1            ; d1 = row_offset + bitplane_base
```

`$b2` is written once in the primary segment (file 0xba2) and is the
runtime pointer to the active bitplane memory.

### Related low-memory (still open)

| Addr | Seen in |
|------|---------|
| `$34c` | second line path (0x29c06) |
| `$356` | second line path |
| `$2a8` | subtracted from $352-derived value |

### Geometric picture

```
Y0 = $350,  Y1 = $352
row_bytes = 40                    ; 320 px bitplane
addr = bitplane_base($b2) + Y1 × 40 + X_byte_offset
height = (Y1 − Y0) + 1
BLTSIZE = (height << 6) | width_bits
```

X-coordinate contribution is still partially opaque (comes in via d2
before the ×40 block and via d6 for the width field of BLTSIZE).
