// Head and eye geometry of --vr (render/vr_math.hpp).
#include "render/vr_math.hpp"

#include <cmath>
#include <cstdio>

using namespace render;
using vr_math::Mat;

static int failures = 0;
static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}
static bool near(double a, double b, double eps = 1e-5) { return std::fabs(a - b) < eps; }

// seat-space direction (x left, y up, z forward) as the head sees it
static void to_head(const HeadView& h, const double v[3], double out[3]) {
  for (int r = 0; r < 3; ++r)
    out[r] = double(h.head_from_seat[size_t(r * 3)]) * v[0] + double(h.head_from_seat[size_t(r * 3 + 1)]) * v[1] +
             double(h.head_from_seat[size_t(r * 3 + 2)]) * v[2];
}

int main() {
  const double deg = 3.14159265358979323846 / 180.0;
  const double half = 45 * deg;
  const double fov[2][4] = {{-half, half, half, -half}, {-half, half, half, -half}};
  const int size[2][2] = {{1000, 1000}, {1000, 1000}};
  const double ahead[3] = {0, 0, 1};
  double v[3];

  {  // head straight: nothing turns, the frustum is the eyes' plus a margin
    const Mat pose[2] = {vr_math::from_angles(0, 0, 0), vr_math::from_angles(0, 0, 0)};
    const HeadView h = vr_math::head_view(pose, fov, size, 0);
    to_head(h, ahead, v);
    check(near(v[0], 0) && near(v[1], 0) && near(v[2], 1), "straight head keeps the view direction");
    check(h.tan_h > 1.0f && h.tan_h < 1.1f && h.tan_v > 1.0f && h.tan_v < 1.1f, "frustum of a 90 degree eye");
    check(near(h.eye[0].tan_left, -1) && near(h.eye[0].tan_up, 1) && h.eye[0].width == 1000, "eye copied");
    check(near(h.eye[0].from_head[0], 1) && near(h.eye[0].from_head[4], 1) && near(h.eye[0].from_head[8], 1),
          "eye looks where the head looks");
  }
  {  // head turned 90 degrees right: the game's forward is on the left
    const Mat p = vr_math::from_angles(90 * deg, 0, 0);
    const Mat pose[2] = {p, p};
    const HeadView h = vr_math::head_view(pose, fov, size, 0);
    to_head(h, ahead, v);
    check(near(v[0], 1) && near(v[1], 0) && near(v[2], 0), "turned right: forward is to the left");
    check(near(vr_math::yaw_of(p), -90 * deg), "yaw of a head turned right");
    // centred on that direction, the head is straight again
    const HeadView c = vr_math::head_view(pose, fov, size, vr_math::yaw_of(p));
    to_head(c, ahead, v);
    check(near(v[0], 0) && near(v[1], 0) && near(v[2], 1), "recentred");
  }
  {  // head up 30 degrees: forward is below the head's forward
    const Mat p = vr_math::from_angles(0, 30 * deg, 0);
    const Mat pose[2] = {p, p};
    const HeadView h = vr_math::head_view(pose, fov, size, 0);
    to_head(h, ahead, v);
    check(near(v[0], 0) && near(v[1], -0.5) && near(v[2], std::cos(30 * deg)), "looking up");
  }
  {  // rolled 90 degrees clockwise: the world's up is to the head's left
    const Mat p = vr_math::from_angles(0, 0, 90 * deg);
    const Mat pose[2] = {p, p};
    const HeadView h = vr_math::head_view(pose, fov, size, 0);
    const double up[3] = {0, 1, 0};
    to_head(h, up, v);
    check(near(v[0], 1) && near(v[1], 0) && near(v[2], 0), "rolled clockwise");
  }
  {  // canted displays: the head is midway, the frustum holds both eyes
    const Mat pose[2] = {vr_math::from_angles(-5 * deg, 0, 0), vr_math::from_angles(5 * deg, 0, 0)};
    const HeadView h = vr_math::head_view(pose, fov, size, 0);
    to_head(h, ahead, v);
    check(near(v[0], 0) && near(v[2], 1), "head midway between canted eyes");
    check(h.tan_h > float(std::tan(50 * deg)) && h.tan_h < float(std::tan(50 * deg) * 1.1), "frustum of canted eyes");
    // the right eye sees the head's forward direction 5 degrees to its left
    const auto& m = h.eye[1].from_head;
    check(near(m[2], std::sin(5 * deg)) && near(m[8], std::cos(5 * deg)), "right eye turned right");
  }
  {  // a quaternion: 90 degrees about the vertical, to the left
    const double s = std::sqrt(0.5);
    const Mat q = vr_math::from_quat(0, s, 0, s);
    check(near(vr_math::yaw_of(q), 90 * deg), "quaternion yaw");
  }
  if (failures) return 1;
  std::printf("vr_view: ok\n");
  return 0;
}
