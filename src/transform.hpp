#pragma once
#include <cstdint>
#include <cmath>
#include <array>
#include "display.hpp"

namespace fa18 {

// ---------------------------------------------------------------------------
// Fixed-point conventions recovered from the Amiga binary (2026-10-12)
//
//   Angle circle : 7200 units = 360°  (20 units/degree) at the table level
//   Builder input: asr #3 before table lookup (may be 57600-circle at source)
//   Sin/cos      : Q2.14  (0x4000 = 1.0)
//   Matrix coeffs: Q2.14  (written by builders A/B)
//   Transform    : (Σ Mij·vj) >> 6   (file 0x7ad8 / live 00C141E0)
// ---------------------------------------------------------------------------

struct Vec3 {
  float x, y, z;
};

struct Mat3 {
  float m[3][3];
};

// Runtime matrix mirror of absolute words $be6–$bf6 (row-major int16)
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

inline Vec3 apply_view(const Vec3& v, int32_t vx, int32_t vy, int32_t vz) {
  // Original masks X/Z with $3fffff; see apply_view_masked in flight.hpp
  return { v.x + float(vx), v.y + float(vy), v.z + float(vz) };
}

// Exact perspective from file 0x2864a
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

inline Mat3Fixed to_fixed(const Mat3& M, float scale = 64.f) {
  Mat3Fixed F{};
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      F.m[r][c] = static_cast<int16_t>(M.m[r][c] * scale);
  return F;
}

// ---------------------------------------------------------------------------
// Q2.14 helpers matching builders at live 00C2CFD2 / 00C2D06C
// ---------------------------------------------------------------------------

constexpr int kAngleCircle = 7200;   // 360° in table units
constexpr int kQuadrant    = 1800;   // 90°
constexpr int16_t kOneQ14  = 0x4000; // 1.0 in Q2.14

inline int16_t mul14(int16_t a, int16_t b) {
  return static_cast<int16_t>((int32_t(a) * int32_t(b)) >> 14);
}

// Soft float sin/cos in Q2.14 for the port (table can replace later).
// angle in 7200-circle units.
inline void sincos_q14(int angle, int16_t& s, int16_t& c) {
  // Normalise to 0..7199
  angle %= kAngleCircle;
  if (angle < 0) angle += kAngleCircle;
  float rad = angle * (2.f * 3.14159265f / float(kAngleCircle));
  s = static_cast<int16_t>(std::sin(rad) * kOneQ14);
  c = static_cast<int16_t>(std::cos(rad) * kOneQ14);
}

// Builder B (live 00C2D07A) — exact element formulas.
// s0,c0 / s1,c1 / s2,c2 = sin/cos of Euler angles 0,1,2.
inline Mat3Fixed builder_B(int16_t s0, int16_t c0,
                           int16_t s1, int16_t c1,
                           int16_t s2, int16_t c2) {
  Mat3Fixed out{};
  // M[0..8] row-major
  out.m[0][0] = static_cast<int16_t>((int32_t(c2)*c1 - int32_t(mul14(s2,s0))*s1) >> 14);
  out.m[0][1] = static_cast<int16_t>(-(int32_t(mul14(c2,s0))*s1 + int32_t(s2)*c1) >> 14);
  out.m[0][2] = mul14(c0, s1);
  out.m[1][0] = mul14(s2, c0);
  out.m[1][1] = mul14(c2, c0);
  out.m[1][2] = s0;
  out.m[2][0] = static_cast<int16_t>(-(int32_t(mul14(s2,s0))*c1 + int32_t(c2)*s1) >> 14);
  out.m[2][1] = static_cast<int16_t>((int32_t(s2)*s1 - int32_t(mul14(c2,s0))*c1) >> 14);
  out.m[2][2] = mul14(c0, c1);
  return out;
}

// Builder A (live 00C2CFE0) — alternate combination pattern.
inline Mat3Fixed builder_A(int16_t s0, int16_t c0,
                           int16_t s1, int16_t c1,
                           int16_t s2, int16_t c2) {
  // D0=s0, D1=c0, D2=s1, D3=c1, D4=s2, D5=c2
  Mat3Fixed out{};
  out.m[0][0] = static_cast<int16_t>((int32_t(mul14(s1,s0))*s2 + int32_t(c1)*c2) >> 14);
  out.m[0][1] = static_cast<int16_t>(-mul14(c0, s2));
  out.m[0][2] = static_cast<int16_t>((int32_t(mul14(c1,s0))*s2 - int32_t(s1)*c2) >> 14);
  out.m[1][0] = static_cast<int16_t>((int32_t(mul14(s1,s0))*c2 - int32_t(c1)*s2) >> 14);
  out.m[1][1] = mul14(c0, c2);
  out.m[1][2] = static_cast<int16_t>((int32_t(mul14(c1,s0))*c2 + int32_t(s1)*s2) >> 14);
  out.m[2][0] = static_cast<int16_t>(-mul14(s1, c0));
  out.m[2][1] = static_cast<int16_t>(-s0);
  out.m[2][2] = mul14(c1, c0);
  return out;
}

// Full pipeline matching the orchestrator at 00C2C4B2:
//   three angles in 7200-circle units → builder B matrix (primary).
// Angle identity (yaw/pitch/roll) still unlabelled — passed as a0,a1,a2.
inline Mat3Fixed mat_from_angles_q14(int a0, int a1, int a2) {
  // asr #3 scaling is applied in the original before table lookup;
  // if callers already pass table-native units, skip it.
  int16_t s0, c0, s1, c1, s2, c2;
  sincos_q14(a0, s0, c0);
  sincos_q14(a1, s1, c1);
  sincos_q14(a2, s2, c2);
  return builder_B(s0, c0, s1, c1, s2, c2);
}

// Legacy helper kept for existing demos (0–255 circle, float path).
inline Mat3Fixed mat_from_angles_fp(uint8_t yaw, uint8_t pitch, uint8_t roll) {
  auto rad = [](uint8_t a) { return a * (2.f * 3.14159265f / 256.f); };
  float cy = std::cos(rad(yaw)),   sy = std::sin(rad(yaw));
  float cp = std::cos(rad(pitch)), sp = std::sin(rad(pitch));
  float cr = std::cos(rad(roll)),  sr = std::sin(rad(roll));
  Mat3 M = {{
    { cy*cp,  cy*sp*sr - sy*cr,  cy*sp*cr + sy*sr },
    { sy*cp,  sy*sp*sr + cy*cr,  sy*sp*cr - cy*sr },
    {   -sp,           cp*sr,            cp*cr    },
  }};
  return to_fixed(M, 64.f);
}

} // namespace fa18
