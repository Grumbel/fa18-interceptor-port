# Draw Modes

## Dispatcher

```
move.w #MODE, $36e
jsr    $12          ; runtime vector → draw engine
```

`$12` reads `$36e` and dispatches to the mode body.

## Complete mode catalogue

Every `move.w #MODE, $36e` site in the binary (45 distinct modes):

| Mode | Dec | File site | Notes |
|------|-----|-----------|-------|
| `$01` | 1 | 0x1c302 | DDA span/poly edge (with muls/divs) |
| `$03` | 3 | 0x1c3f2 | same CODE region as $01 |
| `$04` | 4 | 0x1c4d6 | |
| `$05` | 5 | 0x1c5d4 | |
| `$06` | 6 | 0x1c600 | |
| `$07` | 7 | 0x1c69a | |
| `$08` | 8 | 0x1c736 | |
| `$09` | 9 | 0x1c7d0 | |
| `$0a` | 10 | 0x1c8e6 | |
| `$0c` | 12 | 0x115ee | |
| `$0d` | 13 | 0x115fe | |
| `$0e` | 14 | 0x117ee | |
| `$0f` | 15 | 0x1274c | |
| `$10` | 16 | 0x129ba | |
| `$11` | 17 | 0x12414 | |
| `$12` | 18 | 0x12424 | |
| `$14` | 20 | 0x12d58 | |
| `$15` | 21 | 0x1d3dc | |
| `$16` | 22 | 0x28698 | vertex walker alternate entry |
| `$17` | 23 | 0x13dbc | |
| `$1b` | 27 | 0x28274 | **line** (thin wrapper → jsr $12) |
| `$1c` | 28 | 0x11736 | |
| `$1e` | 30 | 0x2147e | |
| `$1f` | 31 | 0x1c1e0 | |
| `$20` | 32 | 0x238f0 | |
| `$22` | 34 | 0x12fd8 | |
| `$26` | 38 | 0x1261c | |
| `$27` | 39 | 0x10aca | |
| `$28` | 40 | 0xe010 | |
| `$29` | 41 | 0x10fc8 | |
| `$30` | 48 | 0x2b518 | |
| `$33` | 51 | 0xe4bc | |
| `$34` | 52 | 0x1b0f6 | |
| `$35` | 53 | 0x1aaca | |
| `$36` | 54 | 0x1193a | |
| `$37` | 55 | 0x12bd8 | |
| `$38` | 56 | 0x116ee | |
| `$3a` | 58 | 0x1e3b4 | |
| `$3c` | 60 | 0x24f3c | |
| `$3e` | 62 | 0x21a30 | |
| `$3f` | 63 | 0x1b0c | |
| `$40` | 64 | 0x2376a | |
| `$41` | 65 | 0x239c6 | |
| `$42` | 66 | 0x11534 | |
| `$46` | 70 | 0x2c6e2 | |

## Mode $1b – line

```
; file 0x28274 – HUNK_CODE entry
move.w #$1b, $36e
jsr    $12
rts
```

Sole issuer. Lives in HUNK_CODE size 0x205 longwords.

## Mode $16 – vertex walker entry

Alternate entry into the projected-geometry walker at 0x2844e
(perspective + edge helpers + jsr $5ee).

## Mode $01 – DDA span/poly

File 0x1c1f4 region: muls+divs edge slope, then mode $01.

## Vertex walker and projection (CODE hunk 0x2826c)

| Address | Role |
|---------|------|
| 0x2844e | Vertex walker: `lea $3000`, clip tests, bsr edge helpers |
| 0x2864a | Perspective: `(x*160)/z+160`, `(y*90)/z+90` |
| 0x28684 | `movem.w $3000` / `jsr $5ee` |
| 0x28698 | Mode $16 alternate entry |
| 0x286ca/f8/2c/5a | Octant/clip edge helpers |

## $c6 vector installation (file 0x12664)

`$c6` is installed **dynamically** via a table lookup:

```
lea    $151a, a1          ; offset table
move.b $220, d7           ; index
… scale and table lookup …
move.l d6, $c6            ; computed target
```

Not a fixed function pointer — depends on runtime state at `$220`.
