#include "native/gl_camera_interp.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

namespace {

using glr::camera_interp::Matrix;

Matrix yaw(double degrees, const std::array<double, 3>& scale) {
  const double radians = degrees * std::acos(-1.0) / 180.0;
  const double c = std::cos(radians), s = std::sin(radians);
  return {float(scale[0] * c), 0.f, float(scale[0] * s),
          0.f, float(scale[1]), 0.f,
          float(-scale[2] * s), 0.f, float(scale[2] * c)};
}

Matrix pitch(double degrees, const std::array<double, 3>& scale) {
  const double radians = degrees * std::acos(-1.0) / 180.0;
  const double c = std::cos(radians), s = std::sin(radians);
  return {float(scale[0]), 0.f, 0.f,
          0.f, float(scale[1] * c), float(-scale[1] * s),
          0.f, float(scale[2] * s), float(scale[2] * c)};
}

Matrix roll(double degrees, const std::array<double, 3>& scale) {
  const double radians = degrees * std::acos(-1.0) / 180.0;
  const double c = std::cos(radians), s = std::sin(radians);
  return {float(scale[0] * c), float(-scale[0] * s), 0.f,
          float(scale[1] * s), float(scale[1] * c), 0.f,
          0.f, 0.f, float(scale[2])};
}

bool near(double a, double b, double epsilon = 1e-5) {
  return std::abs(a - b) < epsilon;
}

double length(const Matrix& m, int row) {
  double sum = 0;
  for (int col = 0; col < 3; ++col) sum += double(m[row * 3 + col]) * m[row * 3 + col];
  return std::sqrt(sum);
}

double row_dot(const Matrix& m, int a, int b) {
  double sum = 0;
  for (int col = 0; col < 3; ++col)
    sum += double(m[a * 3 + col]) * m[b * 3 + col];
  return sum;
}

}  // namespace

int main() {
  using glr::camera_interp::interpolate;
  constexpr std::array<double, 3> scales{168. / 256, 252. / 256, 128. / 256};
  const Matrix a = yaw(0, scales), b = yaw(90, scales);
  assert(interpolate(a, b, 0) == a);
  assert(interpolate(a, b, 1) == b);
  bool used_rotation = false;
  const Matrix mid = interpolate(a, b, .5f, &used_rotation);
  assert(used_rotation);
  const double half = std::sqrt(.5);
  assert(near(mid[0], scales[0] * half));
  assert(near(mid[2], scales[0] * half));
  assert(near(mid[4], scales[1]));
  assert(near(mid[6], -scales[2] * half));
  assert(near(mid[8], scales[2] * half));
  for (int row = 0; row < 3; ++row)
    assert(near(length(mid, row), scales[size_t(row)]));
  for (int row = 0; row < 3; ++row)
    for (int other = row + 1; other < 3; ++other)
      assert(near(row_dot(mid, row, other), 0));

  for (const Matrix target : {pitch(90, scales), roll(90, scales), yaw(173, scales)}) {
    const Matrix half_turn = interpolate(a, target, .5f);
    for (int row = 0; row < 3; ++row) {
      assert(near(length(half_turn, row), scales[size_t(row)]));
      for (int other = row + 1; other < 3; ++other)
        assert(near(row_dot(half_turn, row, other), 0));
    }
  }
  const Matrix half_pitch = interpolate(a, pitch(90, scales), .5f);
  assert(near(half_pitch[4], scales[1] * half));
  assert(near(half_pitch[5], -scales[1] * half));
  const Matrix half_roll = interpolate(a, roll(90, scales), .5f);
  assert(near(half_roll[0], scales[0] * half));
  assert(near(half_roll[1], -scales[0] * half));

  // Heading wrap must rotate just two degrees across 180, not through 0.
  const Matrix before_wrap = yaw(179, scales), after_wrap = yaw(-179, scales);
  const Matrix across = interpolate(before_wrap, after_wrap, .5f);
  assert(near(across[0], -scales[0]));
  assert(near(across[2], 0));
  assert(near(across[6], 0));
  assert(near(across[8], -scales[2]));

  // Real view matrices retain the original fixed-point row scaling. Scale
  // changes are blended, but do not convert the projection to unit rows.
  const std::array<double, 3> bigger{scales[0] * 2, scales[1] * 1.25,
                                      scales[2] * 1.5};
  const Matrix rescaled = interpolate(a, yaw(90, bigger), .5f);
  for (int row = 0; row < 3; ++row)
    assert(near(length(rescaled, row), (scales[size_t(row)] + bigger[size_t(row)]) / 2));

  // Reject reflections and incomplete guest matrices rather than inventing
  // an orientation. Preserve the original linear path as a safe fallback.
  Matrix reflected = b;
  for (int col = 0; col < 3; ++col) reflected[6 + col] *= -1;
  const Matrix fallback = interpolate(a, reflected, .5f, &used_rotation);
  assert(!used_rotation);
  for (size_t i = 0; i < 9; ++i)
    assert(fallback[i] == a[i] + .5f * (reflected[i] - a[i]));
  Matrix zero{};
  assert(interpolate(a, zero, 1) == zero);
  const Matrix degenerate = interpolate(a, zero, .5f);
  for (size_t i = 0; i < 9; ++i)
    assert(degenerate[i] == a[i] + .5f * (zero[i] - a[i]));

  // Slight row quantisation must still permit a valid rotation.
  Matrix quantized = yaw(47, scales);
  for (float& value : quantized) value = std::round(value * 256.f) / 256.f;
  assert(glr::camera_interp::decompose(quantized).valid);
  assert(interpolate(quantized, b, 0) == quantized);
  assert(interpolate(quantized, b, 1) == b);
}
