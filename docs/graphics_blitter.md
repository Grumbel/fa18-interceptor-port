# Graphics / Blitter Notes

## Custom-chip base references

Many `lea $dff000, a0/a1/a2` instructions appear in the range
**0x288c8 – 0x2aa30**.  Spot checks show classic **blitter-wait** sequences:

```
lea   $dff000, a0
btst  #6, 2(a0)      ; BBUSY (DMACONR)
beq   ready
nop
nop
bra   wait
move.w #$28, 102(a0) ; BLTCON0 / related
move.w #$28,  96(a0)
…
```

This region is therefore a strong candidate for the **2D drawing / polygon
fill / bitplane management** code used by the 3D engine and HUD.

## Picture-wait / early display path (0x0680)

```
0x0680  jsr $ef4
…
0x06a0  jsr $996
0x06ae  jsr $cce
0x06ba  jsr $1c        ; patched by WHDLoad for BUTTONWAIT
…
0x06cc  rts
0x06d6  move.w #3, $112
```

Absolute targets `$ef4`, `$996`, `$cce`, `$1c` are runtime addresses
(fixed-load model).  Once the load base is known they can be mapped back
to file offsets.  The subsequent `move.w #3, $112` looks like a state or
display-mode variable.

## On-disk assets

- `pix/splsh` – empty (0 bytes) on the original ADF
- `pix/frnt5`, `pix/inst5` – small 5-plane ILBMs (HUD / instruments)
- Path strings live at file ~0xd5c1 (`df0:pix/splsh` etc.)

Larger title / cockpit graphics are almost certainly stored as raw
planar data inside the executable or generated procedurally; they are
not present as standalone ILBM files on the disk.

## Next RE targets for graphics

1. Map the absolute call targets from the picture-wait routine to file
   offsets (requires load-base assumption or dynamic tracing).
2. Disassemble the blitter region (0x28800+) to identify bitplane
   pointers, copper-list setup and polygon-fill entry points.
3. Search for copper-list data (moves to $dff080–$dff08c, COLOR registers).

## Picture-wait / title path (updated)

See `docs/load_model.md` for the resolved call chain inside the function
that contains the BUTTONWAIT site.  The private helpers `$996` and `$cce`
are the next place to look for splash / ILBM loading logic.
