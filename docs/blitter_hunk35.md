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
