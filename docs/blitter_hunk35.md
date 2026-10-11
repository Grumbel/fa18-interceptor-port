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
