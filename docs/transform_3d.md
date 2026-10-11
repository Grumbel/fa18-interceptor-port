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
