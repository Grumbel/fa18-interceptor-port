// Head and eye geometry of a head-mounted display, from OpenXR's terms to
// the display list's (display_list.hpp).  No OpenXR or OpenGL in here.
//
// OpenXR: x right, y up, z backward; an orientation rotates vectors of the
// eye into the reference space.  Display list: x left, y up, z forward;
// matrices are row-major and act on column vectors.
//
// The "seat" is the reference space turned about the vertical so that its
// forward direction is where the pilot looked when the view was centred.
// The game's own view direction (forward out of the cockpit, or wherever a
// view key points it) is the seat's forward direction, so the head's
// rotation relative to the seat is applied on top of the game's camera.
#pragma once

#include "render/display_list.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace render {

struct HeadView {
  // Game view -> head (centre of the two eyes).
  std::array<float, 9> head_from_seat{1, 0, 0, 0, 1, 0, 0, 0, 1};
  // Symmetric frustum around the head's forward direction that contains
  // both eyes' pictures (tangents of the half angles).
  float tan_h = 1, tan_v = 1;
  EyeView eye[2];
};

namespace vr_math {

using Mat = std::array<double, 9>;

inline Mat mul(const Mat& a, const Mat& b) {
  Mat r{};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      r[size_t(i * 3 + j)] = a[size_t(i * 3)] * b[size_t(j)] + a[size_t(i * 3 + 1)] * b[size_t(3 + j)] +
                             a[size_t(i * 3 + 2)] * b[size_t(6 + j)];
  return r;
}

inline Mat transpose(const Mat& a) { return {a[0], a[3], a[6], a[1], a[4], a[7], a[2], a[5], a[8]}; }

// Unit quaternion (x, y, z, w) as a rotation matrix.
inline Mat from_quat(double x, double y, double z, double w) {
  const double n = std::sqrt(x * x + y * y + z * z + w * w);
  if (n < 1e-9) return {1, 0, 0, 0, 1, 0, 0, 0, 1};
  x /= n, y /= n, z /= n, w /= n;
  return {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
          2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
          2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
}

// OpenXR axes: rotation about the vertical by `a` radians, positive to the left.
inline Mat about_y(double a) {
  const double c = std::cos(a), s = std::sin(a);
  return {c, 0, s, 0, 1, 0, -s, 0, c};
}

// A head turned `yaw` to the right, `pitch` up and rolled `roll` clockwise
// (radians), in OpenXR axes.
inline Mat from_angles(double yaw, double pitch, double roll) {
  const double cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(-roll), sr = std::sin(-roll);
  const Mat rx{1, 0, 0, 0, cp, -sp, 0, sp, cp}, rz{cr, -sr, 0, sr, cr, 0, 0, 0, 1};
  return mul(about_y(-yaw), mul(rx, rz));
}

// The angle about the vertical of a pose's forward direction (see about_y).
inline double yaw_of(const Mat& pose) { return std::atan2(pose[2], pose[8]); }

// The same rotation between spaces with x left, y up, z forward.
inline std::array<float, 9> to_display(const Mat& m) {
  std::array<float, 9> r{};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) r[size_t(i * 3 + j)] = float(((i == 1) != (j == 1)) ? -m[size_t(i * 3 + j)] : m[size_t(i * 3 + j)]);
  return r;
}

// The orientation midway between the two eyes' (they differ by a few
// degrees at most, on headsets with canted displays).
inline Mat midway(const Mat eye_pose[2]) {
  Mat head{};
  for (size_t i = 0; i < 9; ++i) head[i] = eye_pose[0][i] + eye_pose[1][i];
  // back to a rotation: Gram-Schmidt on the columns
  auto col = [&](int c, double v[3]) { for (int r = 0; r < 3; ++r) v[r] = head[size_t(r * 3 + c)]; };
  auto norm = [](double v[3]) {
    const double n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int r = 0; r < 3; ++r) v[r] = n > 1e-12 ? v[r] / n : (r == 0 ? 1.0 : 0.0);
  };
  double z[3], y[3], x[3];
  col(2, z);
  norm(z);
  col(1, y);
  const double d = y[0] * z[0] + y[1] * z[1] + y[2] * z[2];
  for (int r = 0; r < 3; ++r) y[r] -= d * z[r];
  norm(y);
  x[0] = y[1] * z[2] - y[2] * z[1];
  x[1] = y[2] * z[0] - y[0] * z[2];
  x[2] = y[0] * z[1] - y[1] * z[0];
  return {x[0], y[0], z[0], x[1], y[1], z[1], x[2], y[2], z[2]};
}

// The two eyes (orientation in the reference space, OpenXR axes; field of
// view as angles in radians: left, right, up, down; picture size) and the
// seat's yaw.  `eye_pos` (metres, reference space, OpenXR axes) is where
// the eyes are relative to the point the game's camera stands for; null:
// both at that point (everything at infinity).
inline HeadView head_view(const Mat eye_pose[2], const double fov[2][4], const int size[2][2], double seat_yaw,
                          const double (*eye_pos)[3] = nullptr) {
  HeadView h;
  const Mat head = midway(eye_pose);
  h.head_from_seat = to_display(mul(transpose(head), about_y(seat_yaw)));
  double th = 0, tv = 0;
  for (int e = 0; e < 2; ++e) {
    EyeView& v = h.eye[e];
    v.from_head = to_display(mul(transpose(eye_pose[e]), head));
    v.tan_left = float(std::tan(fov[e][0]));
    v.tan_right = float(std::tan(fov[e][1]));
    v.tan_up = float(std::tan(fov[e][2]));
    v.tan_down = float(std::tan(fov[e][3]));
    v.width = size[e][0];
    v.height = size[e][1];
    if (eye_pos) {
      // into the head's axes (head transposed), then x left and z forward
      double q[3];
      for (int r = 0; r < 3; ++r)
        q[r] = head[size_t(r)] * eye_pos[e][0] + head[size_t(3 + r)] * eye_pos[e][1] + head[size_t(6 + r)] * eye_pos[e][2];
      v.pos = {float(-q[0]), float(q[1]), float(-q[2])};
    }
    // the corners of the eye's picture, as directions of the head
    for (float tx : {v.tan_left, v.tan_right})
      for (float ty : {v.tan_up, v.tan_down}) {
        const double p[3] = {-double(tx), double(ty), 1.0};  // x left
        double q[3];
        for (int r = 0; r < 3; ++r)  // from_head transposed: eye -> head
          q[r] = double(v.from_head[size_t(r)]) * p[0] + double(v.from_head[size_t(3 + r)]) * p[1] +
                 double(v.from_head[size_t(6 + r)]) * p[2];
        const double zf = std::max(q[2], 0.05);
        th = std::max(th, std::fabs(q[0]) / zf);
        tv = std::max(tv, std::fabs(q[1]) / zf);
      }
  }
  // a little more than needed: the original culls with an 8-bit matrix
  h.tan_h = float(std::clamp(th * 1.04, 0.3, 4.0));
  h.tan_v = float(std::clamp(tv * 1.04, 0.3, 4.0));
  return h;
}

}  // namespace vr_math
}  // namespace render
