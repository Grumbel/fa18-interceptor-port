#pragma once
#include <cstdint>
#include <cmath>
#include "display.hpp"

namespace fa18 {

// Mirrors the fixed-point matrix multiply recovered at file 0x7ad8.
// Original uses muls.w + asr #6; we use float for the port.

struct Vec3 {
  float x, y, z;
};

struct Mat3 {
  float m[3][3];
};

inline Vec3 transform_point(const Mat3& M, const Vec3& v) {
  return {
    M.m[0][0]*v.x + M.m[0][1]*v.y + M.m[0][2]*v.z,
    M.m[1][0]*v.x + M.m[1][1]*v.y + M.m[1][2]*v.z,
    M.m[2][0]*v.x + M.m[2][1]*v.y + M.m[2][2]*v.z,
  };
}

// Simple perspective projection onto the 320×180 playfield.
// Focal length chosen so a unit-depth object roughly fills the view.
inline void project(const Vec3& v, int& sx, int& sy, float focal = 200.f) {
  if (v.z <= 0.1f) {
    sx = PLAYFIELD_CX;
    sy = PLAYFIELD_CY;
    return;
  }
  float invz = focal / v.z;
  sx = static_cast<int>(PLAYFIELD_CX + v.x * invz);
  sy = static_cast<int>(PLAYFIELD_CY - v.y * invz);  // Y up in world, down on screen
}

// Identity / simple yaw helper for demos
inline Mat3 mat_yaw(float degrees) {
  float r = degrees * 3.14159265f / 180.f;
  float c = std::cos(r), s = std::sin(r);
  return {{
    { c, 0, s},
    { 0, 1, 0},
    {-s, 0, c},
  }};
}

} // namespace fa18
