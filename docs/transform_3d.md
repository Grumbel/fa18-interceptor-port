# 3D Transform / Projection

## Matrix multiply (file 0x7ad8, runtime `$77d8`)

```
link a6, #-10
… setup …
; input vector from stack locals:
;   X = -2(a6)   Y = -4(a6)   Z = -6(a6)

; ── row 0 → $422 ──
move.w $be6, d0          ; M00
muls.w X, d0
move.w $be8, d2          ; M01
muls.w Y, d2
add.l  d2, d0
move.w $bea, d2          ; M02
muls.w Z, d2
add.l  d2, d0
asr.l  #6, d0
move.l d0, $422          ; out_x

; ── row 1 → $426 ──
move.w $bec, d2          ; M10
muls.w X, d2
move.w $bee, d5          ; M11
muls.w Y, d5
add.l  d5, d2
move.w $bf0, d5          ; M12
muls.w Z, d5
add.l  d5, d2
asr.l  #6, d2
move.l d2, $426          ; out_y

; ── row 2 → $42a ──
move.w $bf2, d0          ; M20
muls.w X, d1             ; (d1 still holds X from earlier reload path)
…
asr.l  #6, d1
move.l d1, $42a          ; out_z
```

### Matrix layout (absolute words)

| Addr   | Element | Notes |
|--------|---------|-------|
| `$be6` | M00     | row0 col0 |
| `$be8` | M01     | |
| `$bea` | M02     | |
| `$bec` | M10     | |
| `$bee` | M11     | |
| `$bf0` | M12     | |
| `$bf2` | M20     | |
| `$bf4` | M21     | |
| `$bf6` | M22     | |

### Fixed-point format

- Coefficients: signed 16-bit
- Accumulate in 32-bit, then **`asr.l #6`** (÷64)
- Effective Q10.6 (or simply “× coeff / 64”)
- Static image at file `0xee6` is **not** the matrix — coefficients are
  written at runtime (no static absolute stores found in primary;
  likely filled by orientation/physics code via register-indirect or
  overlay).

### Results

| Addr  | Meaning |
|-------|---------|
| `$422` | transformed X |
| `$426` | transformed Y |
| `$42a` | transformed Z |

### Caller

`bsr.w $7ad8` at file `0x77f6` inside the object loop (`$72dc`).

## Perspective / projection

Only one `divs` found in the primary segment.  Perspective is likely
done either:

1. With a reciprocal multiply (common on 68000), or
2. Inside an overlay hunk that owns the final screen-space conversion
   before the clamp at hunk 35.

The C++ port currently uses a simple `focal / z` perspective that is
sufficient for the wireframe demo; the exact original method remains open.

## Port mapping

```cpp
// Fixed-point: result = (M * v) >> 6
Vec3 transform_point_fp(const int16_t M[3][3], int16_t x, int16_t y, int16_t z) {
  auto row = [&](int r) {
    return (int32_t(M[r][0])*x + int32_t(M[r][1])*y + int32_t(M[r][2])*z) >> 6;
  };
  return { float(row(0)), float(row(1)), float(row(2)) };
}
```

## Post-multiply: world translation

After the matrix multiply, the transform adds a camera/world offset and
writes the result **into the current object** (`$c`):

```
(object+0) = $422 + ($b68 & $3fffff)   ; world X
(object+4) = $426 + $b6c               ; world Y
(object+8) = $42a + ($b70 & $3fffff)   ; world Z
```

| Addr  | Role |
|-------|------|
| `$b68` | translation X (masked to 22 bits) |
| `$b6c` | translation Y |
| `$b70` | translation Z (masked to 22 bits) |

So `$b68/$b6c/$b70` are the **view/camera translation** applied after
rotation.  The object structure now holds world-space coordinates at
offsets 0 / 4 / 8.

A second partial multiply (subset of the same matrix) then re-writes
`$422/$426/$42a`, and `bsr $7dfe` runs before the transform returns.

## Object structure (partial)

| Offset | Content |
|--------|---------|
| +0     | world X (long, after transform) |
| +4     | world Y |
| +8     | world Z |
| +$10   | (read in post-transform draw path) |
| +$26   | flags / type word (read by transform setup) |
| +$28   | word written by draw path |
| +$30   | word (decremented / clamped in draw path) |

## Post-transform draw path

Object loop calls `bsr` to file **0x784c** (`link a6,#-18`) after the
transform returns.  That routine:

1. Loads object at `$c`
2. Reads flags at +$26 / +$28
3. Calls low vectors `$c6` and `$68a`
4. Manipulates object fields at +$10, +$28, +$30

Screen projection and the eventual blitter call are behind `$c6` /
`$68a` or further in this function — still being traced.

## Perspective divide

The primary-segment transform uses **asr #6** for fixed-point scaling
only — no `divs`/`divu` appears in the matrix multiply or its immediate
post-processing.

Division ops are abundant in overlay hunks (especially around file
offsets 0xaa00–0xad00 and 0xc000–0xc700).  Perspective projection is
therefore likely performed in an overlay, after world-space coordinates
are written to the object structure (+0/+4/+8), and before screen-space
vertices land in the `$3000` buffer.

The C++ port’s `focal / z` stand-in remains a reasonable approximation
until the exact overlay routine is identified.

## Perspective / interpolation divides (file 0x1c1f4)

Function at file **0x1c1f4** (`link a6, #-6`) contains the pattern:

```
muls.w
… abs / shift …
divs.w              ; (a * b) / c
…
move.w #$1, $36e
jsr    $12          ; draw mode 1
```

This is a **geometry primitive** (edge setup or span) that:
1. Computes a fixed-point quotient via muls+divs
2. Draws with mode `$01` (not the line mode `$1b`)

The same muls/divs pair appears twice in the function (two edges or
two axes).  Whether the divisor is a Z (perspective) or an edge
delta (interpolation) is not yet certain; both are common.  Callers
of this function will resolve which.

### Matrix coefficient writers

Still **no absolute stores** to `$be6`–`$bf6` anywhere in the binary.
The matrix is filled exclusively via register-indirect writes
(An-relative), or by an overlay whose load address is unknown.
Candidates for the writer:
- Orientation/physics code that has a pointer to the matrix block
- An init routine that copies a template via `(An)+`

## Draw mode $01

Issued by the function above.  Body not yet identified (lives behind
`$12` dispatcher switch).

## File 0x1c1f4 – edge interpolation (not perspective)

Closer analysis of the muls/divs inputs shows classic **polygon edge
setup**:

```
movem.w (a0)+, d0-d2     ; edge/vertex data
asl.l   #3, d0/d1/d2     ; scale
sub.w   d4, d1           ; deltas
…
muls.w  d5, d1
divs.w  d2, d1           ; slope = (Δ * scale) / Δother
```

This is DDA/Bresenham-style edge stepping for scan conversion, not a
perspective divide.  Mode `$01` is therefore a **filled-span / poly
edge** primitive rather than a projective transform.

True perspective (world Z → screen) remains unlocated; it may use a
reciprocal multiply (no divs) or live entirely in an unresolved overlay.

## True perspective projection (file 0x2864a)

Located in the same CODE hunk as mode `$1b` (HUNK_CODE 0x2826c):

```
; d3 = world/camera X,  d4 = Y,  d5 = Z
muls.w  #160, d3         ; x * focal_x
divs.w  d5, d3           ; / z
addi.w  #160, d3         ; + screen_center_x
cmpi.w  #320, d3         ; clamp to [0, 320)

muls.w  #90, d4          ; y * focal_y
divs.w  d5, d4           ; / z
addi.w  #90, d4          ; + screen_center_y
cmpi.w  #180, d4         ; clamp to [0, 180)

move.w  d3, (a0)+        ; store screen X into vertex buffer
move.w  d4, (a0)+        ; store screen Y
```

### Projection formula

```
screen_x = (x * 160) / z + 160
screen_y = (y *  90) / z +  90
```

Focal lengths equal half the 320×180 viewport — standard symmetric
perspective for this resolution.

### After projection

```
movem.w $3000, d0-d3     ; reload projected vertices
jsr     $5ee             ; runtime vector → line/edge draw
```

`$5ee` receives the screen-space vertex pair and is the bridge into
the blitter line path (or mode `$1b` via `$12`).

### Vertex walker (file 0x2844e)

```
link a6, #-4
lea  $4202, a1           ; edge/object descriptor
lea  $3000, a0           ; vertex buffer
… clip tests against a1+6/+8/+a …
bsr  $286ca / $286f8 / $2872c / $2875a   ; octant/clip helpers
```

Mode `$16` (file 0x28698) is an alternate entry into the same walker.

### C++ equivalent

```cpp
constexpr int kFocalX = 160, kFocalY = 90;
constexpr int kCenterX = 160, kCenterY = 90;

int screen_x = (x * kFocalX) / z + kCenterX;
int screen_y = (y * kFocalY) / z + kCenterY;
// clamp to [0, 320) × [0, 180)
```

## Matrix coefficient writers – status

**No absolute stores** to `$be6`–`$bf6` exist in the entire binary.
**No large muls clusters** that look like rotation-matrix construction
were found in primary or overlay regions.

### Attitude inputs (likely)

| Addr | Role |
|------|------|
| `$4b0` | angle A (0–255) |
| `$4b2` | angle B (0–255) |
| `$29c` / `$29e` | related attitude state |
| `$2a2` / `$2a6` | additional attitude words |

Processed heavily in the function at file **0x404c–0x44e4**, which
does angle normalisation (`andi #$ff`) and state updates but contains
**zero muls** and no stores to the matrix region.

### Implications

The matrix is filled either by:
1. An-relative stores (`move.w Dn, d16(An)` with An pointing at `$be6`)
2. A bulk copy from a precomputed table
3. An overlay whose absolute addresses are relocatable and not visible
   as `$be6` in the static image

Dynamic tracing (breakpoint on write to `$be6`) is the practical next
step.  Until then the C++ port builds the matrix from yaw/pitch/roll
using the same fixed-point scale (×64, matching `asr #6`).

### Transform call site

```
object loop:
  jsr $a0dc / $a2f0     ; object prep
  bsr $7ad8             ; transform (reads $be6–$bf6 → $422/$426/$42a)
  … post-transform 0x784c …
```
## Matrix coefficient writer (dynamic trace, 2026-10-12)

The writer is **absent from the static primary image**; it lives in a
runtime-loaded CODE region.  Located via FS-UAE memory dump + live
breakpoint.

### Live addresses (ADF boot, this session)

| Item | Live address | Notes |
|------|--------------|-------|
| Primary CODE base delta | `+$00C0CA08` | static `$1124` → `00C0DB2C` |
| Data segment delta | `+$00C44368` | static `$2aa` → `00C44612` |
| Matrix base (`$be6`) | `00C44F4E` | 9 × int16 |
| Sin/cos table | `00C3D320` | see below |
| Builder B entry | `00C2D07A` | main fill loop |
| Trig lookup helper | `00C2D232` | angle → sin/cos |
| Transform (`$77d8`) | `00C141E0` | consumer of matrix |
| Object loop (`$72dc`) | `00C13CE4` | |
| Frame (`$1124`) | `00C0DB2C` | |

(These absolute addresses are session-specific; re-derive from the
frame prologue if the load base changes.)

### Sin/cos table (`00C3D320`)

- 16-bit unsigned entries, Q2.14 style (`0x4000` = 1.0)
- Index 0 → 0, index 896 → `0x4000` (exactly 1.0)
- Covers **0° … 90°** in 897 steps
- Quadrant handling done by the lookup helper (negation + index reflection)

Sample head:

```
0000 001D 0039 0056 0072 008F 00AC 00C8 …
```

### Builder B core (live `00C2D07A`)

```
; inputs: D0–D5 hold sin/cos terms (Q2.14), A1 → matrix write ptr
move.w  D4, D6
muls.w  D0, D6
moveq   #14, D7
asr.l   D7, D6          ; ÷ 16384
muls.w  D2, D6
move.w  D5, D7
muls.w  D3, D7
sub.l   D6, D7
moveq   #14, D6
asr.l   D6, D7
move.w  D7, (A1)+       ; store element
… (repeats for remaining elements)
```

Fixed-point: **asr #14** (not the #6 used later by the transform multiply).
Matrix coefficients are therefore Q2.14; the transform path then does
another `asr #6` when applying the matrix to a vertex.

### Trig helper sketch (`00C2D232`)

```
lea     $00C3D320, A0   ; table base
cmp.w   #$0708, D4      ; quadrant / range test
… index arithmetic …
move.w  (A0, D4.w), D4  ; sin or cos
move.w  (A0, D6.w), D5  ; the other
… possible negations …
; falls into (or returns to) builder
```

### Two near-identical builders

| Label | Approx. range | Notes |
|-------|---------------|-------|
| Builder A | `00C2CFE0`–`00C2D06A` | ends with RTS |
| Builder B | `00C2D07A`–… | the one observed writing the live matrix |

Selection criterion (view mode / object type / player vs world) still
open; both use the same table and #14 shift.

### Port mapping (updated)

```cpp
// Q2.14  (1.0 == 0x4000)
constexpr int kOne = 0x4000;
constexpr int kShift = 14;          // builder
constexpr int kXformShift = 6;      // later transform multiply

int16_t sin_table[897];             // 0°..90°, value 0..0x4000

void build_rotation_matrix(/* angles */, int16_t out[9]) {
  // 1. lookup sin/cos via table + quadrant logic (00C2D232)
  // 2. run the muls/asr#14 sequence from 00C2D07A
  //    writing nine int16 coefficients into out[]
}

Vec3 transform_point_fp(const int16_t M[9], int16_t x, int16_t y, int16_t z) {
  auto row = [&](int r) {
    return (int32_t(M[r*3+0])*x + int32_t(M[r*3+1])*y + int32_t(M[r*3+2])*z)
           >> kXformShift;
  };
  return { float(row(0)), float(row(1)), float(row(2)) };
}
```

### Remaining open points

1. Exact angle sources (which low-mem / object fields feed D0–D5).
2. When builder A vs B is chosen.
3. Full quadrant logic of the trig helper (table is only 0..90°).
4. Installation of runtime vectors `$12` / `$5ee` (still zero while
   object count `$b8f` == 0).

## Angle system and trig helper (2026-10-12)

### Angle units

Full circle = **7200 units** (20 units per degree).

| Constant | Value | Meaning |
|----------|-------|---------|
| `$0708` | 1800 | 90° |
| `$0E10` | 3600 | 180° |
| `$1518` | 5400 | 270° |
| `$1C20` | 7200 | 360° |

### Sin/cos table indexing

- Table base: live `00C3D320`
- **Byte** indexed (`MOVE.W (A0, Dn.W), …`)
- 1800 bytes ≡ 900 words cover 0°–90°
- Word value `0x4000` = 1.0 (Q2.14)

### Trig helper (`00C2D232`)

```
add.w   d4, d4              ; index *= 2 (byte offset)
lea     $00C3D320, a0       ; table
cmp.w   #$0708, d4          ; quadrant tests against 1800/3600/5400/7200
… fold index into 0..1800 …
move.w  (a0, d4.w), d4      ; sin or cos
move.w  (a0, d6.w), d5      ; the other
… conditional negations …
rts
```

Four quadrant paths; each loads a complementary pair and applies the
appropriate sign.

### Builder A vs B

Both start with the same setup:

```
asr.w   #3, d0 / d2 / d4    ; (e648 e64a e64c)
bsr     <angle-prep>
bsr     $00C2D232           ; trig helper → D4/D5 = sin/cos
; then muls / asr #14 / (An)+ fill
```

| Builder | Setup entry | Fill entry | Write ptr | Notes |
|---------|-------------|------------|-----------|-------|
| A | `00C2CFD2` | `00C2CFE0` | A0 / A1 | one element-order / sign pattern |
| B | `00C2D06C` | `00C2D07A` | A1 | the one observed writing the live player matrix |

They differ in which sin/cos terms are multiplied into which matrix
slots (standard vs alternate rotation-matrix layout, or view vs object
space). Exact selection criterion still open.

### Angle-prep caller (`00C2C8A6`)

```
movem.l …, -(sp)
btst    #7, (a1)            ; flag on object?
… range-check angle in d4 against $0384 / $0708 / $0a8c …
bsr     $00C2D232           ; get sin/cos
move.w  d4, $22(a1)         ; store sin into object+0x22
move.w  d5, $24(a1)         ; store cos into object+0x24
```

Object structure gains two more known fields:

| Offset | Content |
|--------|---------|
| +$22 | sin(angle) Q2.14 |
| +$24 | cos(angle) Q2.14 |

### Higher-level matrix call (`00C2C4B2`)

```
movem.l d3-d6/a1/a6, -(sp)  ; mask $4e40
move.w  d4, d0
move.w  d5, d2
move.w  d6, d4
lea     $80(a1), a1         ; point at matrix destination?
bsr     $00C2CFD2           ; → builder A path
movem.l (sp)+, …
… optional second bsr $00C2D06C (builder B) …
rts
```

### Port mapping (angle → matrix)

```cpp
constexpr int kCircle = 7200;       // 360°
constexpr int kQuadrant = 1800;     // 90°
constexpr int kOne = 0x4000;        // Q2.14 1.0

int16_t sin_table[900];             // byte-indexed 0..1798

void sincos_q14(int angle, int16_t& s, int16_t& c) {
  // fold angle into 0..7199, then quadrant logic from 00C2D232
  // index = folded * 2 (byte offset into table)
}

void build_matrix_from_angles(/* yaw, pitch, roll in 7200-circle units */,
                              int16_t out[9]) {
  // call sincos for each axis, then builder A or B muls/asr#14 sequence
}
```

## Builder A + B are sequential stages (not alternatives)

The orchestrator at live `00C2C4B2` always runs **both**:

```
movem.l d3-d6/a1/a6, -(sp)
move.w  d4, d0
move.w  d5, d2
move.w  d6, d4
lea     $80(a1), a1          ; matrix destination
bsr     $00C2CFD2            ; ★ Builder A
movem.l (sp)+, …
adda.w  #$92, a1             ; advance to second matrix
clr.l   d0 / d2 / d4
move.w  #$7080, d1
; optional angle tweaks from d5/d6/d7
bsr     $00C2D06C            ; ★ Builder B
rts
```

- Builder A writes ~8 words at `A1` (original + $80)
- A1 advances by **$92** (146 bytes)
- Builder B writes ~8 words at the second location

Two related matrices are produced per call (e.g. view + inverse, or
object-local + camera). Exact geometric roles still to be confirmed
from which address the transform (`$77d8`) reads.

### Call chain into the orchestrator

```
frame $00C0DB2C
  …
  jsr $00C24674 / $00C24676   ; related setup
  jsr $00C2C4F4               ; wrapper → flag test → orchestrator
  jsr $00C1B0A6 / $00C24040   ; post-matrix work
  …
```

Also called from `00C2468C` (secondary path).

### Object structure additions

| Offset | Content |
|--------|---------|
| +$22 | sin(angle) Q2.14 |
| +$24 | cos(angle) Q2.14 |
| +$80 | start of first matrix written by builder A (relative to object ptr used as A1) |

(The +$80 destination is relative to whatever A1 the caller passes;
when A1 is an object pointer this lands inside or just past the
64-byte pool stride — may instead be a global matrix buffer.)

## Builder B element formulas (exact)

After `asr #3` on D0/D2/D4, helper `00C2D14E`, and trig `00C2D232`:

Registers hold Q2.14 sin/cos terms.  All `>>` below are `asr.l #14`.

```
M[0] = ( D5*D3 - ((D4*D0)>>14)*D2 ) >> 14
M[1] = -( ((D5*D0)>>14)*D2 + D4*D3 ) >> 14
M[2] = ( D1*D2 ) >> 14
M[3] = ( D4*D1 ) >> 14
M[4] = ( D5*D1 ) >> 14
M[5] = D0
M[6] = -( ((D4*D0)>>14)*D3 + D5*D2 ) >> 14
M[7] = ( D4*D2 - ((D5*D0)>>14)*D3 ) >> 14
M[8] = ( D1*D3 ) >> 14
```

Nine elements = full 3×3, row-major, written via `(A1)+`.

### Builder A element formulas (exact)

Same setup sequence; different combination pattern:

```
M[0] = ( ((D2*D0)>>14)*D4 + D3*D5 ) >> 14
M[1] = -( D1*D4 ) >> 14
M[2] = ( ((D3*D0)>>14)*D4 - D2*D5 ) >> 14
M[3] = ( ((D2*D0)>>14)*D5 - D3*D4 ) >> 14
M[4] = ( D1*D5 ) >> 14
M[5] = ( ((D3*D0)>>14)*D5 + D2*D4 ) >> 14
M[6] = -( D2*D1 ) >> 14
M[7] = -D0
M[8] = ( D3*D1 ) >> 14
```

(Builder A has a `SWAP D7` mid-sequence that is dead / overwritten by
the following `MOVEQ #14,D7`; it does not affect the result.)

### Port implementation sketch

```cpp
static inline int16_t mul14(int16_t a, int16_t b) {
  return int16_t((int32_t(a) * int32_t(b)) >> 14);
}

void builder_B(int16_t d0, int16_t d1, int16_t d2,
               int16_t d3, int16_t d4, int16_t d5,
               int16_t out[9]) {
  out[0] = int16_t((int32_t(d5)*d3 - int32_t(mul14(d4,d0))*d2) >> 14);
  out[1] = int16_t(-(int32_t(mul14(d5,d0))*d2 + int32_t(d4)*d3) >> 14);
  out[2] = mul14(d1, d2);
  out[3] = mul14(d4, d1);
  out[4] = mul14(d5, d1);
  out[5] = d0;
  out[6] = int16_t(-(int32_t(mul14(d4,d0))*d3 + int32_t(d5)*d2) >> 14);
  out[7] = int16_t((int32_t(d4)*d2 - int32_t(mul14(d5,d0))*d3) >> 14);
  out[8] = mul14(d1, d3);
}
```

Register-to-angle mapping (which of D0–D5 is sin/cos of yaw vs
pitch vs roll) depends on helper `00C2D14E` and is the next target.

## Helper 00C2D14E — dual sincos (angle → register map)

```
; inputs:  D0 = angle_0, D2 = angle_1   (after asr #3)
; outputs: D0 = sin0, D1 = cos0
;          D2 = sin1, D3 = cos1
```

Same quadrant folding as `00C2D232`, applied twice.  First-quadrant
path loads `table[θ]` → sin and `table[90°−θ]` → cos.

### Complete Euler pipeline

```
; three angles arrive in orchestrator as D4, D5, D6
move.w  d4, d0
move.w  d5, d2
move.w  d6, d4

asr.w   #3, d0 / d2 / d4        ; scale
bsr     00C2D14E                ; (sin0,cos0) in D0,D1
                                ; (sin1,cos1) in D2,D3
bsr     00C2D232                ; (sin2,cos2) in D4,D5
; then builder A or B muls
```

| Caller reg | After shuffle | After sincos | Meaning |
|------------|---------------|--------------|---------|
| D4 | D0 | D0=sin0, D1=cos0 | Euler angle 0 |
| D5 | D2 | D2=sin1, D3=cos1 | Euler angle 1 |
| D6 | D4 | D4=sin2, D5=cos2 | Euler angle 2 |

### Builder B with named sin/cos

```
s0,c0 = sin/cos(angle0)   // D0, D1
s1,c1 = sin/cos(angle1)   // D2, D3
s2,c2 = sin/cos(angle2)   // D4, D5

M[0] = ( c2·c1 − ((s2·s0)>>14)·s1 ) >> 14
M[1] = −( ((c2·s0)>>14)·s1 + s2·c1 ) >> 14
M[2] = (c0·s1) >> 14
M[3] = (s2·c0) >> 14
M[4] = (c2·c0) >> 14
M[5] = s0
M[6] = −( ((s2·s0)>>14)·c1 + c2·s1 ) >> 14
M[7] = ( s2·s1 − ((c2·s0)>>14)·c1 ) >> 14
M[8] = (c0·c1) >> 14
```

This is a composed Euler rotation (order determined by which physical
axis each angle drives — still to label as yaw/pitch/roll from
gameplay behaviour).

### asr #3 scaling note

Angles are shifted right by 3 before table lookup, so the **native
angle unit at the orchestrator input** is 8× finer than the table’s
7200-circle unit:

- Table circle = 7200
- Orchestrator circle = 7200 × 8 = **57600** units/360°

Alternatively the stored attitude angles already incorporate this
scale.  Live probing of known attitude values will settle it.

## Transform reads the global matrix at `$be6` (confirmed live)

Live transform at `00C141E0` (static `$77d8`):

```
move.w  $00C44F4E, d0    ; M00  (= static $be6)
muls.w  x, d0
move.w  $00C44F50, d2    ; M01
muls.w  y, d2
add.l   d2, d0
move.w  $00C44F52, d2    ; M02
muls.w  z, d2
add.l   d2, d0
asr.l   #6, d0
move.l  d0, $00C4478A    ; out X (= static $422)

move.w  $00C44F54, d2    ; M10
… → $00C4478E            ; out Y (= $426)

move.w  $00C44F5A, d0    ; M20
… → out Z (= $42a)
```

So the **global matrix** at static `$be6` / live `00C44F4E` is the one
consumed by the object transform.  The dual-builder path that writes at
`A1` / `A1+$92` must point one of its destinations at this global slot
(or a bulk copy follows).  The second matrix ($92 later) is not read by
this transform; its role (inverse, alternate view, object-local) remains
open but is not required for the basic flight path.

## Transform body complete (file 0x7ad8) — world store

After the matrix×vector multiply into `$422/$426/$42a`:

```
movea.l $c, a0
move.w  $b5a, $30(a0)        ; copy counter scratch → object+0x30
move.w  $b5c, $32(a0)        ; → object+0x32

; X
move.l  $b68, d0
andi.l  #$3fffff, d0         ; mask view X
add.l   $422, d0
move.l  d0, (a0)             ; object+0 = transformed_x + view_x

; Y
move.l  $426, d0
add.l   $b6c, d0
move.l  d0, 4(a0)            ; object+4

; Z (same pattern with $b70 & $3fffff → object+8)
```

View/camera translation is absolute `$b68/$b6c/$b70`.  X and Z are
masked to 22 bits (`$3fffff`) before add; Y is unmasked.

### Prologue flag paths

Before the multiply, the routine inspects object+0x26 bits and may
short-circuit with fixed local vector components (`#$fff8`, `#1`,
`#$18`, etc.) written into stack locals that feed the muls.  Several
object-type / state combinations skip the full matrix path.

## Alternate geometry path at 0x28374

Inside the geometry hunk, a second routine also does perspective and
`jsr $5ee` without going through the walker at 0x2844e:

```
lea $4202, a1
; read vertex pairs from (a1)+
; perspective-style muls/divs with 160/90
; clamp
jsr $5ee
```

Likely a simpler “already-projected or pre-clipped edge list” drawer
that still uses `$4202` as its source descriptor.

## Euler angle labels (yaw / pitch / roll)

Isolating each builder-B angle with the other two at identity:

| Angle | Single-axis matrix | Body axis | Aircraft name |
|-------|--------------------|-----------|---------------|
| **angle0** (D4→sincos→s0,c0) | R_x | X forward | **roll** |
| **angle1** (D5→sincos→s1,c1) | R_y | Y right | **pitch** |
| **angle2** (D6→sincos→s2,c2) | R_z | Z down | **yaw** |

Evidence (float-equivalent of builder B):

- only angle0: `M = [[1,0,0],[0,c,s],[0,−s,c]]` → R_x
- only angle1: `M = [[c,0,s],[0,1,0],[−s,0,c]]` → R_y
- only angle2: `M = [[c,−s,0],[s,c,0],[0,0,1]]` → R_z

Composition is the full builder-B product (not a naive R_z R_y R_x
multiply order), but the **axis identity of each angle is fixed**.

### Input axes vs Euler angles

| Low-mem | 8-bit range | Feeds | Label |
|---------|-------------|-------|-------|
| `$4b0` | 0–255 | angle path (via 0x404c) | primary stick — **pitch-like in port**, needs live confirm |
| `$4b2` | 0–255 | secondary | secondary stick |

`$4b0` / `$4b2` are **not** the 7200-circle Euler angles.  They are
8-bit input axes that a conversion path (including possible `asr #3`
scaling into the 57600-unit circle) turns into angle0/1/2.

### Port mapping (corrected)

```cpp
// mat_from_angles_q14(angle0, angle1, angle2)
//   angle0 = roll   (R_x)
//   angle1 = pitch  (R_y)
//   angle2 = yaw    (R_z)
```

### Status

- [x] angle0 = roll (R_x)
- [x] angle1 = pitch (R_y)
- [x] angle2 = yaw (R_z)
- [ ] Live-trace which of `$4b0`/`$4b2`/other feeds which angle
- [ ] Confirm stick polarity in-game

## Attitude state → matrix orchestrator (partial)

### Player / camera structure at `$00C44EBC`

Indexed by `$00C44616` (0 in this dump).  World position lives at
structure+`$14` (= `$00C44ED0`, matches known world X).

| Struct off | Live address | In-flight | Role |
|------------|--------------|-----------|------|
| +`$66` | `$00C44F22` | `$6D78` | related |
| +`$68` | `$00C44F24` | **`$08C0`** (2240) | **Euler-like angle** (~112° in 7200-circle) |
| +`$6A` | `$00C44F26` | `$0058` | related |
| +`$6C` | `$00C44F28` | `$295F` | related |

### Path into orchestrator

```
lea     $00C44EBC, a0
adda.w  $00C44616, a0
move.w  $68(a0), d4          ; angle0 candidate → orchestrator D4
; … similar for other angles …
jsr     $00C2C4F4            ; wrapper → orchestrator $00C2C4B2
```

`$00C44662` is a working copy: `move.w $68(a1), $00C44662` at
`$00C08AF6` / `$00C08DF6`.

### `$4b0` / `$4b2` remain upstream

Raw 8-bit axes at `$00C44818` / `$1A` are written by input paths
(e.g. `$00C24260`, `$00C1071E`) and consumed by the control decoder
(`$00C10754`).  One branch indexes a byte table at `$00C3BED8` into
`$00C4459A`/`$9B`; physics at `$00C30Exx` then uses those as rate
selectors.  **The integration from rate → structure+$68 is not yet
fully traced** (register-relative stores, no absolute writes to
`$00C44F24` found).

### Working model for the port

```
stick $4b0/$4b2  (0–255)
    → control decoder / rate tables
    → attitude rates
    → integrate into structure+$68/+…  (7200-circle units)
    → orchestrator D4/D5/D6
    → builder A/B → matrix $be6
```

Until the integrator is pinned, the C++ port continues to drive
`mat_from_angles_q14(roll, pitch, yaw)` from keyboard/demo axes
directly (already mapped: angle0=roll, angle1=pitch, angle2=yaw).

### Status

- [x] structure+$68 holds a 7200-circle attitude angle mid-flight
- [x] orchestrator reads +$68 into D4
- [x] `$00C44662` is a shadow copy of +$68
- [x] `$4b0` writers identified (input + control paths)
- [ ] Integrator that writes structure+$68 from rates
- [ ] Which of +$68/+$6A/+$6C is roll vs pitch vs yaw

## Attitude integrator at `$00C2C418`

Located inside a larger control/physics routine that uses **a1 = player
structure** (`$00C44EBC` family).

### Core integration

```asm
; $00C2C416
move.w  d5, d1
sub.w   d1, $68(a1)        ; angle0 -= rate5
bge.s   *+4
neg.w   d1                 ; |Δ| for clamp tests

cmpi.w  #1200, d1          ; 60° in 7200-circle
ble.s   next
cmpi.w  #27600, d1
bge.s   next
addq.w  #1, d0
subi.w  #14400, d1         ; 2×7200 — wrap-related
; …

move.w  d6, d2
sub.w   d2, $6A(a1)        ; angle1 -= rate6
; same clamp family (1200 / 27600 / 14400)
```

| Operation | Field | Rate reg | Meaning |
|-----------|-------|----------|---------|
| `sub.w d5, $68(a1)` | +`$68` | **d5** | primary attitude axis |
| `sub.w d6, $6A(a1)` | +`$6A` | **d6** | secondary attitude axis |

### Clamp / wrap constants (7200-circle units)

| Value | Degrees | Role |
|------:|--------:|------|
| 1200 | 60° | small-delta threshold |
| 800 | 40° | cross-axis compare |
| 6000 | 300° | gate on +`$66` |
| 14400 | 720° (2 turns) | wrap subtract |
| 22800 | 1140° | gate on +`$66` |
| 27600 | 1380° | large-delta reject |

### Related structure fields (in-flight)

| Off | Live | Value | Notes |
|-----|------|------:|-------|
| +`$56` | `$00C44F12` | 4 | rate/flag cluster |
| +`$58`/`$5A` | `$00C44F14`/`16` | 0 | rate/flag cluster |
| +`$66` | `$00C44F22` | 28024 | integrator gate input |
| +`$68` | `$00C44F24` | **2240** | angle0 (integrated) |
| +`$6A` | `$00C44F26` | **88** | angle1 (integrated) |
| +`$6C`/`$6E` | `$00C44F28`/`2A` | 10591 | further attitude state |

### Chain (updated)

```
$4b0/$4b2 (8-bit stick)
  → control decoder / rate tables ($00C4459A …)
  → rates in d5/d6 (and struct +$56..)
  → integrator $00C2C418:  +$68 -= d5,  +$6A -= d6
  → orchestrator reads +$68 → D4
  → builder A/B → matrix
```

### Status

- [x] Integrator found: `sub.w` of rates into +`$68` / +`$6A`
- [x] Clamp constants mapped to 7200-circle
- [ ] Exact source of d5/d6 rates from stick (same function, earlier)
- [ ] Label +`$68`/+`$6A` as roll/pitch/yaw from axis identity

## Attitude rate fields and filter (`$00C2C1F0`)

### Structure layout (player/camera at `$00C44EBC`)

| Off | Role | In-flight |
|-----|------|----------:|
| +`$50` | raw rate contribution 0 (from stick curve) | 0 |
| +`$52` | raw rate contribution 1 | 0 |
| +`$54` | raw rate contribution 2 | 296 |
| +`$56` | **filtered rate 0** (integrated into +`$68`) | 4 |
| +`$58` | **filtered rate 1** (integrated into +`$6A`) | 0 |
| +`$5A` | **filtered rate 2** | 0 |
| +`$68` | attitude angle 0 | 2240 |
| +`$6A` | attitude angle 1 | 88 |

### Stick → raw rate

`$00C2B6CA`: `move.w d3, $50(a1)` then clears +`$52`/+`$54` for a
single-axis path.  d3 comes from a curve table indexed by stick/flag
state (`$00C2B68E` family).  Other sites write +`$52`/+`$54` for the
remaining axes.

### Low-pass rate filter (`$00C2C1F0`)

```asm
; d0,d2,d4 = new deltas (from +$50/+$52/+$54 or prior rates)
; d3       = scale 5/10/15 (from +$26 or flag path), clamped ≤15
ext.l   d0 / d2 / d4
divs.w  d3, d0 / d2 / d4          ; scale new deltas

movem.w $56(a1), d5-d7            ; load old filtered rates
add.w   d0, d5  /  asr #1, d5     ; blend
add.w   d5, d0  /  asr #1, d0
move.w  d0, $56(a1)               ; store smoothed rate 0
; same pattern for d2→+$58, d4→+$5A
```

Result: exponential-ish smoothing of stick-driven rates before
integration.

### Chain (complete middle section)

```
stick $4b0/$4b2
  → curve table → raw rates +$50/+$52/+$54
  → scale by d3 (5..15) + low-pass filter
  → filtered rates +$56/+$58/+$5A
  → integrator: +$68 -= rate0,  +$6A -= rate1
  → orchestrator D4 ← +$68
  → matrix builders
```

### Status

- [x] +`$50`/`$52`/`$54` = raw rates; +`$56`/`$58`/`$5A` = filtered
- [x] Low-pass filter at `$00C2C1F0` decoded
- [x] Stick-curve writer at `$00C2B6CA` for rate0
- [ ] Full three-axis stick identity (which stick axis → which rate)
- [ ] Label angles +`$68`/+`$6A` as R/P/Y

## Three parallel rate-axis writers

| Site | Stores | Clears | Axis |
|------|--------|--------|------|
| `$00C2B6CA` | d3 → +`$50` | +`$52`, +`$54` | **0** |
| `$00C2B3F0` | d3 → +`$52` | +`$54`, +`$50` | **1** |
| `$00C2B554` | d3 → +`$54` | +`$52`, +`$50` | **2** |

Each path:
1. Selects a curve table from `$62(a1)` mode:
   - `$62 & $F0 == $10` → `$00C2B782` (steep: 160…1280)
   - `$62 == $14` → `$00C2B798` (medium: 80…640)
   - else → `$00C2B7C4` (gentle: 8…88)
2. Indexes the table by stick-derived d3
3. Writes the curve output as that axis’s raw rate

### Curve table samples (`$00C2B782`)

```
160, 320, 480, 560, 640, 720, 800, 960, 1040, 1120, 1280, …
```

Non-linear stick response (dead-zone / progressive).

### Integration mapping

| Filtered rate | Integrator target | Notes |
|---------------|-------------------|-------|
| +`$56` (from raw 0) | +`$68` angle0 | confirmed `sub.w d5, $68(a1)` |
| +`$58` (from raw 1) | +`$6A` angle1 | confirmed `sub.w d6, $6A(a1)` |
| +`$5A` (from raw 2) | +`$6C`? | not yet seen in integrator; mid-flight raw +`$54`=296 active |

### Mid-flight snapshot

| Field | Value | Interpretation |
|-------|------:|----------------|
| +`$50`/`$52` | 0 | raw 0/1 idle |
| +`$54` | 296 | raw 2 active |
| +`$56` | 4 | filtered 0 small |
| +`$68` | 2240 | angle0 ~112° |
| +`$6A` | 88 | angle1 ~4.4° |

### Axis identity (still open)

`$4b0` / `$4b2` are the two 8-bit sticks.  Three rate axes exist, so
at least one is a derived/composite (rudder from stick+pedal, or
auto-trim).  Pinning roll vs pitch vs yaw needs a controlled dump
with one stick axis at a time, or a cross-ref from the HUD ladder /
artificial-horizon draw path that reads the same angles.

### Status

- [x] Three exclusive rate writers (axis 0/1/2)
- [x] Curve table selection by `$62` mode
- [x] Rate0→angle0, rate1→angle1 confirmed
- [ ] Rate2→angle2 path
- [ ] Stick axis ↔ rate axis identity

## Rate auto-center (`$00C2C476`)

After the main integrator, filtered rates are nudged toward zero:

```asm
move.w  $00C07384, d0      ; live $0090 = 144
tst.w   $56(a1)
bge.s   *+4
neg.w   d0
add.l   d0, $56(a1)        ; rate0 += sign-adjusted bias
; same for +$58 (rate1)
```

`$00C07384` is a global centering strength (144 in this dump).  This is
a weak spring pulling rates back to zero when the stick is released.

## Input variable map

| Addr | Name | Mid-flight | Writers | Role |
|------|------|----------:|---------|------|
| `$00C44818` | `$4b0` | 0 | `$00C24260`, `$00C1071E` | primary 8-bit stick axis |
| `$00C4481A` | `$4b2` | 0 | `$00C10916`, `$00C10AC2` | secondary 8-bit stick axis |
| `$00C448A0` | — | `$0017` | `$00C24218`, `$00C24250` | processed stick (post-curve in input path) |
| `$00C44604` | — | `$0840` | `$00C24266` | input flags (bit 0 set on stick write) |
| `$00C07384` | — | `$0090` | (static/init) | rate auto-center magnitude |

### Stick → rate path (current understanding)

```
hardware stick
  → input path $00C24260:  $4b0 = raw, $448A0 = processed, flags
  → control decoder $00C10754 (reads $4b0, sets tables at $4459A)
  → rate writers $00C2B6CA/B3F0/B554
      (index from internal d3, NOT direct $4b0 absolute read)
  → raw +$50/+$52/+$54
  → filter → +$56/+$58/+$5A
  → integrate → +$68/+$6A
  → auto-center rates
```

The rate writers take their curve index from register d3 which is
computed earlier in the same large function from structure fields
(matrix elements at +$96.. and mode flags).  **Direct stick→d3 link
still needs a controlled one-axis dump to pin identity.**

### Status

- [x] Rate auto-center at `$00C2C476` with global `$00C07384`
- [x] Input variable map ($4b0, $4b2, $448A0, $44604)
- [ ] Stick↔rate axis identity (blocked on dynamic evidence)

## Joystick input (JOY0DAT)

Hardware read at `$00C15C88`:

```asm
move.w  JOY0DAT, d0          ; $DFF00A
move.w  d0, d1
andi.w  #$FF, d0             ; X (left/right)
asr.w   #8, d1
andi.w  #$FF, d1             ; Y (up/down)
move.w  d0, $00C1975E        ; prev/raw X
move.w  d1, $00C19760        ; prev/raw Y
```

Delta path `$00C15CF0`:

1. Compute delta vs previous `$00C1975E`/`$60`
2. Clamp each axis to ±`$7F`
3. Accumulate into **`$00C44AE`** (X) and **`$00C44B0`** (Y)
4. Apply soft limits from `$00C06CE4`/`$E6`/`$E8`/`$EA`
5. Store back current raw to `$00C1975E`/`$60`

| Addr | Role | Mid-flight |
|------|------|----------:|
| `$00C1975E` | JOY raw X | `$0023` |
| `$00C19760` | JOY raw Y | `$0025` |
| `$00C44AE` | JOY X accumulator | `$0001` |
| `$00C44B0` | JOY Y accumulator | `$8080` |

`$00C44AE` is also folded into `$00C44856` (abs) by `$00C30710` before a call to `$00C24560`.

### Relation to earlier `$4b0`/`$4b2`

`$00C44818` (`$4b0`) and `$00C4481A` (`$4b2`) are a **different** input path (written by `$00C24260`, consumed as throttle-like by `$00C11198` into `$00C44804`/`$08`).  Not the JOY0DAT stick.

### Stick → rate status

JOY axes are now located.  Link from `$00C44AE`/`$44B0` into the three rate writers (`+$50/+$52/+$54`) still needs a cross-ref (no absolute read of these in the `$00C2Bxxx` region — may pass through intermediate structure fields or registers).

### Status

- [x] JOY0DAT → raw X/Y → accumulators `$00C44AE`/`$44B0`
- [x] `$4b0` path distinguished as non-JOY (throttle-like)
- [ ] Accumulator → rate writer link

## Rate computation is orientation-derived

Rate writer prologue at `$00C2B630` does **not** read JOY accumulators
or `$4b0`.  Instead:

```asm
move.w  $96(a1), d0          ; matrix / attitude block
move.w  $9C(a1), d1
move.w  $A2(a1), d4
muls    d5, d0
muls    d6, d1
muls    d7, d4
; sum → magnitude in d3 (after swap → table index 0..10)
; sign from d2
; curve table lookup → raw rate
```

So the stick-curve tables scale a **projected orientation error** (or
body-axis component), not a raw stick deflection.  JOY axes likely
steer a **desired attitude** target that the matrix projection is
measured against, or modulate the d5/d6/d7 weights.

### Rate-writer exclusivity (confirmed)

At `$00C2B54A` (mode nibble `$10` path):

```asm
move.w  d3, $54(a1)          ; rate2 only
clr.w   $52(a1)
clr.w   $50(a1)
```

Each curve path writes exactly one of +`$50`/+`$52`/+`$54` and clears
the other two — three exclusive axis channels.

### Updated stick→rate model

```
JOY0DAT → $44AE/$44B0 (accumulators)
                ↓
        desired-attitude or weight (not yet found)
                ↓
matrix × weights → magnitude → curve table → raw rate
                ↓
filter → integrate → angles
```

### Status

- [x] Rate magnitude from matrix projection, not direct stick
- [x] Exclusive one-of-three rate write confirmed in mode-$10 path
- [ ] Desired-attitude / weight source from JOY accumulators

## Attitude field ↔ Euler axis map (live structure)

| Field | Off | Euler | Axis | Integration |
|-------|-----|-------|------|-------------|
| raw_rate0 | +`$50` | roll rate | R_x | → filter → +`$56` |
| raw_rate1 | +`$52` | pitch rate | R_y | → filter → +`$58` |
| raw_rate2 | +`$54` | yaw rate | R_z | → filter → +`$5A` |
| angle0 | +`$68` | **roll** | R_x | `-= rate0` at `$00C2C418` |
| angle1 | +`$6A` | **pitch** | R_y | `-= rate1` at `$00C2C418` |
| +`$6C` | +`$6C` | **yaw?** | R_z | **not** in `$00C2C418`; mid-flight `$295F` |

Mid-flight snapshot: rate2=296 (yaw active), rate0/1≈0; angle0=2240, angle1=88.  Yaw angle source for the matrix orchestrator’s D6 is still open — may be +`$6C` via a separate path, or derived.

### Status

- [x] angle0/1 = roll/pitch with integrator linkage
- [x] rate2 filtered to +`$5A` (yaw rate)
- [ ] yaw angle (D6) source; rate2→angle2 path
