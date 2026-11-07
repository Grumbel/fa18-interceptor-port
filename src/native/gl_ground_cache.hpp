// The terrain vertices depend on camera translation and the water-tile
// altitude threshold, not on viewport size, view rotation or interpolation
// alpha alone. Reuse the already-uploaded buffer when these inputs match.
#pragma once

#include <cstdint>

namespace glr {

struct GroundCacheKey {
  double x = 0, z = 0, ground_y = 0;
  int32_t altitude = 0;
  bool full_detail = false;

  bool operator==(const GroundCacheKey&) const = default;
};

}  // namespace glr
