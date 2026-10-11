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

## Vector install family at file 0x125d4 / 0x12644 (refined)

Two parallel installs share the same table and index:

```
; --- install $be ---
lea     $151a, a1
move.b  $220, d7          ; index
ext.w   d7
add.w   d7, d7
add.w   d7, d7            ; ×4
move.w  (a1, d7.w), d7    ; table word (extension 7000)
… scale / combine with base regs D2/D4 …
move.l  d6, $be           ; file 0x125f6

; --- install $c6 ---
lea     $151a, a1
move.b  $220, d7
… same ×4 index …
move.w  2(a1, d7.w), d7   ; table word +2 (extension 7002)
… scale / combine with base regs D3/D5 …
move.l  d6, $c6           ; file 0x12664
```

`$be` and `$c6` are **sibling vectors** computed from adjacent words of
the same table entry.  Bases come from registers loaded earlier in the
function (overlay/hunk load addresses).

Immediately after the `$be` install the same region issues:

```
move.w  #$26, $36e        ; mode $26
jsr     $12               ; draw dispatch
```

So `$12` is expected to already be valid when this path runs.  No
`MOVE.L …, $12` or `MOVE.L …, $5ee` exists anywhere in the static
binary (full scan of abs.l stores to dest < $200).  Those two vectors
are installed only from overlay / relocated code not present as fixed
absolute stores in the primary image.

### Low vectors with static install sites

| Addr | Installed at (file) | Notes |
|------|---------------------|-------|
| `$be` | 0x125f6 | sibling of `$c6`, table `$151a` |
| `$c6` | 0x12664 | post-transform draw bridge |
| `$c`  | 0x762a | current object pointer |
| `$b2` | 0xba2 | bitplane base (from earlier RE) |
| `$12` | **none static** | overlay-only |
| `$5ee`| **none static** | overlay-only |

### Port implication

For the C++ port, `$c6` can be modelled as a function pointer set when
the “geometry hunk” is logically active.  Candidate targets are the
perspective/walker entry points in the geometry CODE hunk (file
0x2864a / 0x2844e).  `$12` remains the mode dispatcher front-end.

## Geometry CODE hunk pipeline (file 0x2826c+, size 0x205 longs)

```
HUNK_CODE @ 0x2826c
│
├─ 0x28274  mode $1b wrapper
│             move.w #$1b, $36e
│             jsr $12
│             rts
│
├─ 0x2844e  vertex walker
│             link a6, #-4
│             lea $4202, a1        ; edge / clip descriptor
│             lea $3000, a0        ; projected vertex buffer
│             ; clip tests vs a1+6 / +8 / +$a
│             bsr 0x286ca / 0x286f8 / 0x2872c / 0x2875a  ; octant helpers
│             → falls into perspective
│
├─ 0x2864a  perspective
│             screen_x = (x * 160) / z + 160   ; divs, clamp [0,320)
│             screen_y = (y *  90) / z +  90   ; clamp [0,180)
│             move.w d3, (a0)+     ; store sx
│             move.w d4, (a0)+     ; store sy
│             movem.w $3000, …     ; reload projected pair
│             jsr $5ee             ; ★ edge/line handoff
│             unlk / moveq #1,d0 / rts
│
├─ 0x28698  mode $16 entry
│             move.w #$16, $36e
│             bra 0x28448          ; into walker
│
└─ 0x286ca..0x2875a  four octant / clip helpers
              (sign flips + muls/divs for edge subdivision)
```

### $4202 edge descriptor (fields used by walker)

| Offset | Use |
|--------|-----|
| +6 | clip bound (compared to projected coord) |
| +8 | clip bound |
| +$a | clip bound |

Exact semantics (min/max x/y vs viewport) still partly open; values are
read as words and used in signed compares before the octant BSRs.

### $3000 vertex buffer

Filled by perspective as packed screen-space word pairs `(sx, sy)`.
`$5ee` receives the current pair via `movem.w $3000, …` immediately
before the call.

### How this connects to the object loop

```
object_loop
  transform $77d8       → world coords in object+0/+4/+8
  post-transform 0x784c → jsr $c6
                            │
                            ▼
                    (live $c6 target — likely enters geometry path
                     or sets up $3000 / $4202 then calls walker)
                            │
                            ▼
                    walker → perspective → jsr $5ee → line draw
```

Static analysis cannot resolve `$c6`/`$5ee` targets (overlay installs).
The geometry hunk itself is the clear implementation of projection and
edge setup once those vectors point here.

## $4202 usage summary

| Site (file) | Access |
|-------------|--------|
| 0x28452 | walker: `lea $4202, a1` then `6(a1)` / `8(a1)` / `$a(a1)` as clip bounds |
| 0x28374 | alternate drawer: `lea $4202, a1` then sequential `(a1)+` vertex reads |
| 0x15654, 0x15d40, 0x16d72, 0x16dde | primary-segment LEAs (setup / other modes) |
| 0x25b76 | another LEA A1 |

Clip fields read by the walker:

| Offset | Register | Role |
|--------|----------|------|
| +6 | D2 | bound A (compared around projected X/Y) |
| +8 | D2 | bound B (alternate octant path) |
| +$a | D6 | bound C |

Full structure of `$4202` (whether it also holds edge endpoints, counts,
or flags beyond the three bounds) is not yet exhausted; the alternate
path at 0x28374 treats it as a stream of vertex words via `(a1)+`.
