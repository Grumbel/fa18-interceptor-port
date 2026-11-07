#include "cockpit/globe_model.hpp"

#include <cassert>
#include <cmath>

int main() {
  using namespace cockpit::globe;
  static_assert(within_globe(198, 163) && within_globe(220, 183));
  static_assert(!within_globe(197, 170) && !within_globe(221, 170));
  static_assert(!within_globe(208, 162) && !within_globe(208, 184));
  static_assert(is_fill_pen(17) && is_fill_pen(21) && !is_fill_pen(0));

  assert(degrees(0) == 0 && degrees(7200) == 90);
  assert(degrees(14400) == -180 && degrees(28800) == 0);
  assert(degrees(-7200) == -90 && degrees(36000) == 90);

  // Level attitude matches the original ILBM's blue-above/red-below split.
  assert(palette_index(kBluePen, 208, 170, 0, 0) == kBluePen);
  assert(palette_index(kRedPen, 208, 175, 0, 0) == kRedPen);
  // Moving the boundary *up* makes some former sky become ground.
  assert(palette_index(kBluePen, 208, 170, -4, 0) == kRedPen);
  // A quarter-turn rotates the split into a vertical left/right boundary.
  assert(palette_index(kBluePen, 200, 173, 0, 7200) == kBluePen);
  assert(palette_index(kRedPen, 216, 173, 0, 7200) == kRedPen);
  // Anti-aliased coverage is continuous at the boundary, and shrinks from
  // source-pixel thickness to one device pixel as resolution increases.
  assert(sky_coverage(209, 173, 0, 0, 3) == 0.5);
  assert(sky_coverage(209, 172, 0, 0, 3) == 1.0);
  assert(sky_coverage(209, 174, 0, 0, 3) == 0.0);
  assert(sky_coverage(209, 173 - 1.0 / 12, 0, 0, 3) > 0.5);
  assert(sky_coverage(209, 173 + 1.0 / 12, 0, 0, 3) < 0.5);
  // Existing overlay pixels remain the source art, even at extreme bank.
  assert(palette_index(3, 200, 170, 7, 11000) == 3);
  assert(palette_index(kBluePen, 197, 170, 7, 11000) == kBluePen);
  assert(palette_index(kRedPen, 221, 170, 7, 11000) == kRedPen);
  // Signed distance is periodic even for negative/beyond-circle inputs.
  assert(std::abs(boundary_distance(200, 170, 1, 1000) -
                  boundary_distance(200, 170, 1, 29800)) < 1e-12);
  assert(std::abs(boundary_distance(200, 170, 1, 1000) -
                  boundary_distance(200, 170, 1, -27800)) < 1e-12);
}
