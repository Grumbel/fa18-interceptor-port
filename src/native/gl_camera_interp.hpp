// Interpolate the game's row-scaled world-to-view camera matrix. The three
// rows at $5aed0 are *not* unit vectors: perspective uses independent X/Y/Z
// scale factors. Element-wise interpolation of the complete matrix shears
// and shrinks the view during turns, especially near a 180-degree wrap.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace glr::camera_interp {

using Matrix = std::array<float, 9>;
using Quaternion = std::array<double, 4>;  // x, y, z, w

struct Rotation {
  std::array<double, 9> matrix{};
  std::array<double, 3> scale{};
  bool valid = false;
};

inline std::array<double, 3> cross(const std::array<double, 3>& a,
                                   const std::array<double, 3>& b) {
  return {a[1] * b[2] - a[2] * b[1],
          a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}

inline double dot(const std::array<double, 3>& a,
                  const std::array<double, 3>& b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline bool normalize(std::array<double, 3>& v) {
  const double len = std::sqrt(dot(v, v));
  if (!std::isfinite(len) || len < 1e-8) return false;
  for (double& x : v) x /= len;
  return true;
}

inline Rotation decompose(const Matrix& m) {
  Rotation out;
  std::array<std::array<double, 3>, 3> rows{};
  for (size_t i = 0; i < 3; ++i) {
    for (size_t j = 0; j < 3; ++j) rows[i][j] = m[i * 3 + j];
    const double length = std::sqrt(dot(rows[i], rows[i]));
    if (!std::isfinite(length) || length < 1e-8) return out;
    out.scale[i] = length;
    for (double& value : rows[i]) value /= length;
  }
  // Reject non-rotation transforms (e.g. a wrong or transient guest matrix)
  // instead of silently changing its meaning. Quantisation of the original
  // signed 8.8 entries produces small, acceptable row-dot errors.
  if (std::abs(dot(rows[0], rows[1])) > .12 ||
      std::abs(dot(rows[0], rows[2])) > .12 ||
      std::abs(dot(rows[1], rows[2])) > .12 ||
      dot(cross(rows[0], rows[1]), rows[2]) < .8) return out;
  // Reorthogonalise before converting to a quaternion; carrying the Amiga's
  // fixed-point row errors through a quaternion conversion is unstable.
  const double d = dot(rows[1], rows[0]);
  for (size_t i = 0; i < 3; ++i) rows[1][i] -= d * rows[0][i];
  if (!normalize(rows[1])) return out;
  rows[2] = cross(rows[0], rows[1]);
  for (size_t i = 0; i < 3; ++i)
    for (size_t j = 0; j < 3; ++j)
      out.matrix[i * 3 + j] = rows[i][j];
  out.valid = true;
  return out;
}

inline Quaternion quaternion(const std::array<double, 9>& m) {
  Quaternion q{};
  const double trace = m[0] + m[4] + m[8];
  if (trace > 0) {
    const double s = 2 * std::sqrt(1 + trace);
    q = {(m[7] - m[5]) / s, (m[2] - m[6]) / s,
         (m[3] - m[1]) / s, s / 4};
  } else if (m[0] > m[4] && m[0] > m[8]) {
    const double s = 2 * std::sqrt(1 + m[0] - m[4] - m[8]);
    q = {s / 4, (m[1] + m[3]) / s, (m[2] + m[6]) / s, (m[7] - m[5]) / s};
  } else if (m[4] > m[8]) {
    const double s = 2 * std::sqrt(1 + m[4] - m[0] - m[8]);
    q = {(m[1] + m[3]) / s, s / 4, (m[5] + m[7]) / s, (m[2] - m[6]) / s};
  } else {
    const double s = 2 * std::sqrt(1 + m[8] - m[0] - m[4]);
    q = {(m[2] + m[6]) / s, (m[5] + m[7]) / s, s / 4, (m[3] - m[1]) / s};
  }
  return q;
}

inline Quaternion slerp(Quaternion a, Quaternion b, double t) {
  double cosine = 0;
  for (int i = 0; i < 4; ++i) cosine += a[i] * b[i];
  // Both q and -q encode the same rotation: select the shorter arc through
  // the +/-180-degree wrap, rather than rotating a complete revolution.
  if (cosine < 0) {
    for (double& x : b) x = -x;
    cosine = -cosine;
  }
  cosine = std::clamp(cosine, 0.0, 1.0);
  double x = 1 - t, y = t;
  if (cosine < .9995) {
    const double theta = std::acos(cosine);
    const double inv_sine = 1 / std::sin(theta);
    x = std::sin((1 - t) * theta) * inv_sine;
    y = std::sin(t * theta) * inv_sine;
  }
  Quaternion out{};
  double length2 = 0;
  for (int i = 0; i < 4; ++i) {
    out[i] = x * a[i] + y * b[i];
    length2 += out[i] * out[i];
  }
  const double inv_length = 1 / std::sqrt(length2);
  for (double& v : out) v *= inv_length;
  return out;
}

inline std::array<double, 9> rotation_matrix(const Quaternion& q) {
  const double x = q[0], y = q[1], z = q[2], w = q[3];
  return {1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y),
          2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
          2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)};
}

inline Matrix interpolate(const Matrix& from, const Matrix& to, float t,
                          bool* used_rotation = nullptr) {
  if (used_rotation) *used_rotation = false;
  // Return the original fixed-point sampled coefficients *exactly* at real
  // scene boundaries. Only extra presentations need a reconstructed pose.
  if (t <= 0) return from;
  if (t >= 1) return to;
  Matrix linear{};
  for (size_t i = 0; i < 9; ++i)
    linear[i] = from[i] + t * (to[i] - from[i]);
  const Rotation a = decompose(from), b = decompose(to);
  if (!a.valid || !b.valid) return linear;
  const auto r = rotation_matrix(slerp(quaternion(a.matrix), quaternion(b.matrix), t));
  if (used_rotation) *used_rotation = true;
  Matrix out{};
  for (size_t i = 0; i < 3; ++i) {
    const double scale = a.scale[i] + double(t) * (b.scale[i] - a.scale[i]);
    for (size_t j = 0; j < 3; ++j)
      out[i * 3 + j] = float(r[i * 3 + j] * scale);
  }
  return out;
}

}  // namespace glr::camera_interp
