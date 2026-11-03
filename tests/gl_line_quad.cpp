#include "native/gl_line_quad.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <utility>

namespace {
using glr::line_quad::Vec3;

void test(Vec3 a, Vec3 b, double width = 1) {
  std::array<Vec3, 6> quad{};
  assert(glr::line_quad::make(a, b, quad, width));
  auto sx = [](Vec3 p) { return -160.0 * p.x / p.z; };
  auto sy = [](Vec3 p) { return -90.0 * p.y / p.z; };
  auto near = [](double l, double r) { assert(std::fabs(l - r) < 1e-8); };
  for (auto [offset, end] : {std::pair{0, a}, std::pair{4, b}}) {
    // The two opposite corners project around the original endpoint.
    const Vec3 l = quad[size_t(offset)], r = quad[size_t(offset) + 1];
    near((sx(l) + sx(r)) / 2, sx(end));
    near((sy(l) + sy(r)) / 2, sy(end));
    near(std::hypot(sx(l) - sx(r), sy(l) - sy(r)), width);
  }
  // Triangle adjacency has identical corner values, so no cracks along
  // the diagonal (also validates the triangle construction's shared edge).
  near(quad[1].x, quad[3].x);
  near(quad[2].y, quad[5].y);
}
}  // namespace

int main() {
  auto near_width = [](double a, double b) { assert(std::fabs(a - b) < 1e-8); };
  near_width(glr::line_quad::stroke_width(1), 1.5);
  near_width(glr::line_quad::stroke_width(2), 0.75);
  near_width(glr::line_quad::stroke_width(3), 0.55);
  near_width(glr::line_quad::stroke_width(8), 0.55);
  test({0, 0, 100}, {10, 0, 100});
  test({0, 0, 100}, {0, 20, 100});
  test({-3, 5, 100}, {22, 18, 800});  // perspective and depth variation
  test({-3, 5, 100}, {22, 18, 800}, 1.0 / 3);  // one hi-res pixel at 3x
  test({-3, 5, 100}, {22, 18, 800}, glr::line_quad::stroke_width(3));
  for (const Vec3 p : {Vec3{0, 0, 100}, Vec3{12, -21, 900}}) {
    const auto quad = glr::line_quad::square(p, 2.0);
    auto sx = [](Vec3 v) { return -160.0 * v.x / v.z; };
    auto sy = [](Vec3 v) { return -90.0 * v.y / v.z; };
    near_width(std::fabs(sx(quad[0]) - sx(quad[1])), 2.0);
    near_width(std::fabs(sy(quad[1]) - sy(quad[2])), 2.0);
    near_width((sx(quad[0]) + sx(quad[2])) / 2, sx(p));
    near_width((sy(quad[0]) + sy(quad[2])) / 2, sy(p));
  }
  std::array<Vec3, 6> out{};
  assert(!glr::line_quad::make({1, 2, 100}, {2, 4, 200}, out));
}
