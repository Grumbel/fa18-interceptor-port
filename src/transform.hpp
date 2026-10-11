#pragma once
#include <cstdint>
#include <cmath>
#include "display.hpp"

namespace fa18 {

// Mirrors the fixed-point 3×3 matrix multiply recovered at file 0x7ad8.
// Original: muls.w + accumulate + asr.l #6  (coefficients are int16).

struct Vec3 {
  float x, y, z;
};

struct Mat3 {
  float m[3][3];
};

// Runtime matrix mirror of absolute words $be6–$bf6
struct Mat3Fixed {
  int16_t m[3][3];
};

inline Vec3 transform_point(const Mat3& M, const Vec3& v) {
  return {
    M.m[0][0]*v.x + M.m[0][1]*v.y + M.m[0][2]*v.z,
    M.m[1][0]*v.x + M.m[1][1]*v.y + M.m[1][2]*v.z,
    M.m[2][0]*v.x + M.m[2][1]*v.y + M.m[2][2]*v.z,
  };
}

// Exact original fixed-point path: (Σ Mij * vj) >> 6
inline Vec3 transform_point_fp(const Mat3Fixed& M, int16_t x, int16_t y, int16_t z) {
  auto row = [&](int r) -> float {
    int32_t acc = int32_t(M.m[r][0]) * x
                + int32_t(M.m[r][1]) * y
                + int32_t(M.m[r][2]) * z;
    return float(acc >> 6);
  };
  return { row(0), row(1), row(2) };
}

// Apply view/camera translation (mirrors $b68/$b6c/$b70 add after multiply).
inline Vec3 apply_view(const Vec3& v, int32_t vx, int32_t vy, int32_t vz) {
  return { v.x + float(vx), v.y + float(vy), v.z + float(vz) };
}

// Exact perspective recovered from file 0x2864a:
//   screen_x = (x * 160) / z + 160
//   screen_y = (y *  90) / z +  90
// Focal = half of 320×180 viewport.  Clamped to playfield.
inline void project(const Vec3& v, int& sx, int& sy) {
  if (v.z <= 0.1f) {
    sx = PLAYFIELD_CX;
    sy = PLAYFIELD_CY;
    return;
  }
  sx = static_cast<int>((v.x * 160.f) / v.z + 160.f);
  sy = static_cast<int>((v.y *  90.f) / v.z +  90.f);
  if (sx < 0) sx = 0;
  if (sx >= PLAYFIELD_W) sx = PLAYFIELD_W - 1;
  if (sy < 0) sy = 0;
  if (sy >= PLAYFIELD_H) sy = PLAYFIELD_H - 1;
}

inline Mat3 mat_yaw(float degrees) {
  float r = degrees * 3.14159265f / 180.f;
  float c = std::cos(r), s = std::sin(r);
  return {{
    { c, 0, s},
    { 0, 1, 0},
    {-s, 0, c},
  }};
}

// Build a fixed-point matrix from a float matrix (scale ≈ 64 so that
// >> 6 restores approximately unit range).
inline Mat3Fixed to_fixed(const Mat3& M, float scale = 64.f) {
  Mat3Fixed F{};
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      F.m[r][c] = static_cast<int16_t>(M.m[r][c] * scale);
  return F;
}

} // namespace fa18
