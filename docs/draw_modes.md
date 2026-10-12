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

## Live `$4202` mapping (in-flight dump)

| Item | Value |
|------|-------|
| Static | `$4202` |
| Live | **`$00C4B2CA`** |
| Confirmed via | `lea $4202, a1` at file `0x28452` / `0x28374` / `0x25B76` in dump |

### In-flight field snapshot

| Off | Value | Notes |
|-----|-------|-------|
| +0 | `$0EEE` (3822) | |
| +2 | `$FCA4` (−860) | |
| +4 | `$F6EA` (−2326) | |
| +6 | `$0976` (2422) | clip bound A (walker D2) |
| +8 | `$FECF` (−305) | clip bound B |
| +$a | `$FF6F` (−145) | clip bound C (walker D6) |
| +$c… | 0 | rest clear in this snapshot |

Bounds are signed and not simple viewport min/max (2422 exceeds
playfield width).  They may be fixed-point or object-local clip
values written by the active draw mode before the walker runs.

The alternate path at `0x28374` streams `(a1)+` from this same base —
so +0..+4 may be vertex data or a count/header when that path is
active.  In this dump only +0..+$a are nonzero.

## Live `$12` resolution (in-flight dump)

| Item | Value |
|------|-------|
| Live target of `jsr $12.L` | **`$00C062C2`** |
| Body | **`rts`** (no-op stub) |
| Chip RAM `$12` | `$081C00FC` (unrelated; not used by abs.l sites) |
| Mode register `$36e` | live `$00C446D6` = `$0000` in this snapshot |

### Mode $1b wrapper (confirmed)

```asm
$00C2D7C8  move.w  #$001B, $00C446D6   ; set mode register
           jsr     $00C062C2            ; $12 → rts
           moveq   #0, d0
           rts
```

### Interpretation

In this airborne snapshot `$12` is a **stub**.  Mode wrappers still
write `$36e` and call `$12`, but the dispatcher body is `rts`.

Possible reasons:

1. Draw dispatch was torn down / not needed in the current game phase
2. Real mode bodies are entered by other paths (walker, `$5ee`,
   perspective) that do not go through `$12`
3. `$12` is patched to a real dispatcher only during specific
   draw-setup phases

The substantial code immediately before the stub (`$00C062B0`:
`jsr $00C15FE4 / bsr / jsr $00C15FAE / rts`) may be a related
setup routine, not the dispatcher itself.

### Status

- [x] Live target identified (`$00C062C2`)
- [x] Body is `rts` in this dump
- [x] Mode register live address confirmed (`$00C446D6`)
- [ ] Capture a dump during active line-draw to see if `$12` is
      repatched to a real jump table

## Live mode entry points (in-flight)

Mode setters are thin: `move.w #MODE, $36e` then `jsr $12` (currently
rts) or a branch into shared setup.  Real work is in the bodies that
the setters fall into or that are reached independently.

| Mode | File setter | Live (geometry/DDA base) | Real body |
|------|-------------|--------------------------|-----------|
| `$01` | `0x1C302` | DDA hunk `$00C231E0` | DDA at file `0x1C1F4` – reads `$3C00` edge list |
| `$16` | `0x28698` | geometry `$00C2D7C8` | falls into walker; no `jsr $12` |
| `$1b` | `0x28274` | `$00C2D7C8` | thin: set mode + `jsr $12` (stub) + rts |
| walker | `0x2844E` | **`$00C2D9A2`** | edge walk + clip + `bsr` perspective/`$5ee` |

### Vertex walker (file `0x2844E`, live `$00C2D9A2`)

```asm
link    a6, #-4
lea     $4202.L, a1          ; edge descriptor (live $00C4B2CA)
lea     $3000.L, a0          ; screen-space vertex buffer
move.w  #1, -2(a6)           ; loop / edge counter
movem.w (a1)+, d3-d5         ; load first vertex pair from descriptor

cmp.w   d5, d3
blt.s   path_lo
; path_hi: d3 >= d5
move.w  6(a1), d2            ; clip bound A ($4202+6)
move.w  10(a1), d6           ; clip bound C ($4202+$a)
cmp.w   d2, d6
ble.s   next_edge
bsr.w   perspective_or_edge  ; → +$27C region
beq.w   done
tst.w   $49A.L               ; flag
bge.s   skip
neg.w   d2
cmp.w   d2, d6
ble.s   skip
bsr.w   alt_edge             ; → +$2AA
…
```

Uses `$4202` fields +6 / +8 / +$a as signed clip bounds (confirmed
live values 2422 / −305 / −145).  On accept, branches into
perspective setup and ultimately `jsr $5ee` for span output.

### DDA body (file `0x1C1F4`)

```asm
link    a6, #-6
clr.w   d7
lea     $3C00.L, a0          ; edge / span parameter list
move.w  (a0)+, -2(a6)
move.w  (a0)+, d0
cmpi.w  #3, d0
blt.s   …                    ; need ≥3 points for poly
move.w  d0, -4(a6)
lea     $3600.L, a1          ; working buffer
lea     $1A2.L, a2
lea     $198.L, a3
lea     $FC.L, a4
; … muls/divs DDA edge walk …
```

Mode `$01` setter at `0x1C302` writes `$36e` and calls `$12` (stub),
then `bra` back into this DDA region — so the real work is the
fall-through / branch target, not the `$12` call.

### Implication of `$12` = rts

Mode setters that only `jsr $12` and `rts` (like `$1b`) currently
do nothing.  Modes that `bra` into a local body after the stub call
(like `$01`) still run their algorithm.  Mode `$16` skips `$12`
entirely and branches straight to the walker.

## Walker → perspective → `$5ee` call chain

```
walker $00C2D9A2 (file 0x2844E)
  │
  ├─ clip test vs $4202+6 / +$a
  │
  ├─ bsr 0x286CA  edge_interpolate_A
  │    movem.w 6(a1), d0-d2
  │    muls / divs perspective
  │    bra 0x2878A  (accept/reject)
  │
  └─ bsr 0x286F8  edge_interpolate_B  (symmetric / alternate octant)
       same muls/divs pattern
       bra store path
            │
            ▼
       0x2867A..0x28696  store + $5ee
         move.w d3, (a0)+        ; sx → $3000 buffer
         move.w d4, (a0)+        ; sy
         subq.w #1, -2(a6)       ; edge count--
         beq.s  done
         movem.w $3000, d0-d3    ; reload pair
         jsr    $5ee.L           ; → live $00C2E5D6 span helper
         unlk   a6
         moveq  #1, d0           ; success
         rts
```

### Perspective constants (file `0x28660`)

```
divs.w  d5, d4
addi.w  #$005A, d4        ; +90  (screen Y centre)
blt.s   reject
cmpi.w  #$00B4, d4        ; 180  (screen height bound?)
bge.s   reject
subi.w  #$013F, d3        ; -319 (near 320 screen width)
neg.w   d3
subi.w  #$00B3, d4        ; -179
neg.w   d4
move.w  d3, (a0)+         ; store sx
move.w  d4, (a0)+         ; store sy
```

Screen-space bias uses **90** and **180** (vertical) and **~$13F**
(≈319, horizontal).  Matches the 320×200-class playfield with a
centred projection.

### Edge interpolate core (file `0x286CA`)

```
movem.l d0-d7, -(sp)
movem.w 6(a1), d0-d2      ; from $4202 descriptor
; delta setup …
muls.w  d0, d4
divs.w  d3, d4            ; perspective divide
muls.w  d0, d6
divs.w  d3, d6
bra.w   accept_test
```

Two octant variants (`0x286CA` / `0x286F8`) share the same divide
pattern; the walker selects based on vertex order and clip signs.

### Status

- [x] Walker entry live `$00C2D9A2`
- [x] Clip against `$4202` bounds
- [x] Edge interpolate with perspective muls/divs
- [x] Screen bias constants 90 / 180 / ~319
- [x] Store to `$3000` vertex buffer
- [x] `jsr $5ee` for span output
- [ ] Exact fixed-point format of pre-perspective coords
- [ ] Port walker+perspective into C++ draw path

## `$4202` structure (refined)

### Layout (12-byte records)

From the loop bound at file `0x25B76` (`cmpa.l #$420E, a1` →
`$420E − $4202 = 12`):

| Offset | Alternate drawer (`0x28374`) | Walker (`0x2844E`) |
|--------|------------------------------|-------------------|
| +0 | X  `(a1)+ → d0` | `movem (a1)+ → d3` |
| +2 | Y  `(a1)+ → d1` | `→ d4` |
| +4 | Z  `(a1)+ → d2` | `→ d5` |
| +6 | (next / clip) | after inc: base of remaining; `6(a1)` reads **+12** (next record X) |
| +8 | | |
| +$a | | `10(a1)` reads **+16** |

So `$4202` is a **stream of 3D vertices** (X, Y, Z words), not a
fixed viewport structure.  The “clip bounds” observed earlier are
either neighbouring vertex components or path-specific compares
against other vertices in the list.

### Alternate drawer perspective (exact)

```asm
lea     $4202, a1
move.w  (a1)+, d0            ; X
move.w  (a1)+, d1            ; Y
move.w  (a1)+, d2            ; Z
; clip tests …
muls.w  #160, d0             ; $A0
divs.w  d2, d0
addi.w  #160, d0             ; sx = X*160/Z + 160
; clamp to [0, $13F]
muls.w  #90, d1              ; $5A
divs.w  d2, d1
addi.w  #90, d1              ; sy = Y*90/Z + 90
; clamp to [0, $B4)
; … store / jsr $5ee …
```

This is the classic centred projection already mirrored by
`project()` in `transform.hpp` (160/90).  Confirms playfield
centre = (160, 90) and focal lengths 160 / 90.

### In-flight snapshot values

| Off | Value | As |
|-----|------:|----|
| +0 | 3822 | X |
| +2 | −860 | Y |
| +4 | −2326 | Z (behind or large depth) |
| +6.. | 2422, −305, −145 | next vertex / residual |
| +$c… | 0 | end of active list |

### Status

- [x] 12-byte record size (`$4202`..`$420E`)
- [x] +0/+2/+4 = X/Y/Z vertex
- [x] Alternate path exact perspective (×160/÷Z+160, ×90/÷Z+90)
- [x] Matches existing C++ `project()`
- [ ] Walker’s use of `6(a1)`/`10(a1)` after post-inc (next-record vs bounds)

## Mode setters vs real bodies (DDA region)

All of modes `$01`, `$03`–`$0a`, `$1f` in the DDA hunk are **pure setters**:

```asm
move.w #MODE, $00C446D6   ; $36e
jsr    $00C062C2          ; $12 → currently RTS
```

They do **not** contain unique drawing algorithms.  The real DDA work
lives at a single shared entry that is **called directly** (not via `$12`).

### Shared DDA body — live `$00C231F8`

| Item | Value |
|------|-------|
| Entry | **`$00C231F8`** |
| Direct `jsr` sites | 7 (`$00C1F6CC`, `$00C1F762`, `$00C1F898`, `$00C1FA08`, `$00C1FC26`, `$00C1FD18`, `$00C29B3A`) |
| Edge-list source | **`$00C4ACC8`** |
| Scratch A | `$00C4A6C8` |
| Scratch B | `$00C4D652` / `$00C4D648` / `$00C4D5AC` |

#### Algorithm outline

```asm
link    a6, #-6
clr.w   d7
lea     $00C4ACC8, a0
move.w  (a0)+, -2(a6)      ; header word
move.w  (a0)+, d0          ; vertex/edge count
cmpi.w  #3, d0
blt     fallback           ; need ≥ 3
move.w  d0, -4(a6)

lea     $00C4A6C8, a1
lea     $00C4D652, a2
lea     $00C4D648, a3
lea     $00C4D5AC, a4
clr.l   (a2)
clr.l   4(a4)

; ── per-edge loop ──
movem.w (a0)+, d0-d2       ; load vertex components
asl.l   #3, d0             ; ×8 scale
asl.l   #3, d1
asl.l   #3, d2
; DDA slope setup (muls/divs, sign handling)
; …
bsr     $00C23318          ; span/plot helper
; loop until count exhausted
```

Fixed-point: coordinates are scaled **×8** (`asl.l #3`) before the DDA
step, matching the earlier observation that mode `$01` does muls/divs
edge interpolation.

#### Port implication

```cpp
// Mode numbers $01/$03–$0a/$1f only select a draw style flag.
// The actual edge-list DDA is invoked directly:
void dda_edge_list(const int16_t* edges /* at $00C4ACC8 */);
```

When `$12` is a live dispatcher (not RTS), it may also route to this
body for certain modes — that path needs an active-draw dump to confirm.

### Status

- [x] Modes `$01`,`$03`–`$0a`,`$1f` confirmed pure setters
- [x] Shared DDA body at `$00C231F8` with 7 direct callers
- [x] Edge list `$00C4ACC8`, ×8 coordinate scale
- [ ] Full DDA slope/interpolation math (helper `$00C23318`)
- [ ] Remaining modes outside DDA region (`$0c`–`$0e`, `$0f`–`$26`, …)

## DDA slope helper `$00C23318` / span twin `$00C2340A`

Two near-identical routines implement **fixed-point edge interpolation**
(Bresenham-style with `muls`/`divs` and remainder rounding).

### `$00C23318` — primary edge step

```asm
movem.w d0-d2, -(sp)          ; save
movem.w (a3), d0-d2           ; load current edge state from a3 scratch
tst.b   1(a4)
bne.s   not_first
; first-edge path: seed from a2+offsets, mark a4+1
bra     tail

not_first:
; sign-normalise so primary delta is positive
neg.w   d1 / d4               ; conditional on comparisons
; …
sub.w   d4, d1                ; Δ primary
neg.w   d2
add.w   d5, d2
sub.w   d1, d2                ; Δ secondary (denominator)
beq     degenerate

add.w   d4, d5
muls.w  d5, d1                ; numerator
move.w  d2, d6
bge.s   *+4
neg.w   d6
asr.w   #1, d6                ; |denom|/2  (rounding threshold)
divs.w  d2, d1                ; quotient in low, remainder in high
swap    d1
; remainder rounding: if |rem| > |denom|/2 → adjust quotient ±1
swap    d1
add.w   d4, d1                ; apply step

; same pattern for the orthogonal component (d0 / d3):
sub.w   d3, d0
muls.w  d5, d0
divs.w  d2, d0
; … identical remainder rounding …
add.w   d3, d0

movem.w d0-d2, (a3)           ; store updated edge state
bsr     $00C2340A             ; twin span step
addq.b  #1, 5(a4)             ; edge counter
```

### Rounding rule

After `divs`, the remainder (high word post-`swap`) is compared against
half the absolute denominator (`asr #1`).  If `|rem| > |denom|/2` the
quotient is nudged by ±1 according to the remainder’s sign.  This is
symmetric nearest-integer rounding for the slope step.

### `$00C2340A` — orthogonal twin

Same structure with register roles swapped (X-major ↔ Y-major).  The
pair covers all octants: the primary helper picks the dominant axis,
the twin advances the other.

### Registers on entry (from parent DDA body)

| Reg | Role after ×8 scale |
|-----|---------------------|
| d0, d1, d2 | current vertex components |
| d3, d4, d5 | previous / edge-delta components |
| a2 | scratch `$00C4D652` |
| a3 | edge-state `$00C4D648` |
| a4 | flags/counters `$00C4D5AC` |

### Port mapping

```cpp
// Symmetric-rounded fixed-point edge step (one axis)
int16_t dda_step(int16_t cur, int16_t delta_num, int16_t delta_den) {
    if (delta_den == 0) return cur;
    int32_t prod = int32_t(delta_num) * /* run length or paired delta */;
    int16_t q = prod / delta_den;
    int16_t r = prod % delta_den;
    int16_t half = abs(delta_den) / 2;
    if (abs(r) > half)
        q += (r > 0) ? 1 : -1;
    return cur + q;
}
```

Exact register-to-axis assignment (which of d0/d1 is X vs Y) follows
the parent’s vertex load order from `$00C4ACC8` and is the next
refinement target.

### Status

- [x] `$00C23318` slope math decoded (muls/divs + half-denom rounding)
- [x] `$00C2340A` confirmed orthogonal twin
- [x] Degenerate-delta and first-edge seed paths identified
- [ ] Exact X/Y register assignment from edge-list format
- [ ] C++ port of full edge-list walker

## Edge list format at `$00C4ACC8`

```
+0  .w  header (always 0 in observed writers)
+2  .w  vertex count N  (DDA requires N ≥ 3)
+4  N × 3 words:  (c0, c1, c2) per vertex
```

### Writers

| Site | Behaviour |
|------|-----------|
| `$00C1F642` | `move.l #$00000004, (a0)+` — hard-coded 4-vertex seed |
| `$00C084BE` | count from `(a2)+`, then copy triplets from a source stream via a3=`$00C470C8` scratch |
| `$00C08516` / `$00C08562` | same family as `$00C084BE` |

### In-flight sample

```
header=0  count=4
  ( 3798,  -865, -2314)
  ( 3822,  -860, -2326)
  ( 2422,  -305, -4049)
  ( 4014, -1600,  -130)
  …
```

Values are signed 16-bit, magnitude thousands — **view-space or
pre-perspective coordinates**, not screen pixels.  The DDA body then
applies `asl.l #3` (×8) before slope math.

### Component assignment

Parent loads `movem.w (a0)+, d0-d2` → **d0=c0, d1=c1, d2=c2**.
The slope helper treats (d0,d3) and (d1,d4) as the two interpolated
axes with d2/d5 as the third (depth or parameter).  Given the
perspective pipeline uses X/Y with Z for divide, the natural mapping
is:

| Reg after load | Likely | Evidence |
|----------------|--------|----------|
| d0 | X | first in stream; orthogonal twin adjusts it with d3 |
| d1 | Y | second; primary helper’s dominant axis in one octant |
| d2 | Z / param | third; used as denominator side in slope setup |

Exact screen-axis binding still depends on which octant path the sign
tests select; for the C++ port, treat the stream as **XYZ view-space
triplets** and run ×8 → DDA → optional perspective.

### Port sketch

```cpp
struct EdgeVertex { int16_t x, y, z; };

struct EdgeList {
  int16_t header = 0;
  std::vector<EdgeVertex> verts;  // size N ≥ 3
};

// Filled by geometry path; consumed by dda_edge_list() at $00C231F8
void dda_edge_list(const EdgeList& el) {
  if (el.verts.size() < 3) return;
  for (size_t i = 1; i < el.verts.size(); ++i) {
    auto a = el.verts[i-1], b = el.verts[i];
    // ×8 scale
    a.x*=8; a.y*=8; a.z*=8; b.x*=8; b.y*=8; b.z*=8;
    // dda_axis_step along dominant axis …
  }
}
```

### Status

- [x] Edge list header + count + 3-word vertices
- [x] Writers at `$00C084BE` family and `$00C1F642`
- [x] In-flight sample recovered
- [x] d0/d1/d2 ← c0/c1/c2; XYZ hypothesis
- [ ] Confirm XYZ vs screen-space with a known primitive (runway/cube)
