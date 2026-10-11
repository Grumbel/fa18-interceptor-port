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
