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
