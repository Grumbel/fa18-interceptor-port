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
  test({0, 0, 100}, {10, 0, 100});
  test({0, 0, 100}, {0, 20, 100});
  test({-3, 5, 100}, {22, 18, 800});  // perspective and depth variation
  test({-3, 5, 100}, {22, 18, 800}, 1.0 / 3);  // one hi-res pixel at 3x
  std::array<Vec3, 6> out{};
  assert(!glr::line_quad::make({1, 2, 100}, {2, 4, 200}, out));
}
