// Read-only classifier for the lower panel's *remaining* bitmap consumers.
// The Amiga changes both palette indices and per-row copper colours. Count
// each separately so an instrument's moving ink is not confused with a
// palette fade or the ADF's original half-bright colour mapping.
#pragma once

#include "native/cockpit_assets.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace remaster::cockpit_dynamic_audit {

struct Region {
  const char* name;
  int x0, y0, x1, y1;  // Half-open Amiga 320x200 coordinates.
};

// The artificial horizon (blue/red globe) and the rightmost purple status
// annunciators are dynamic, even though both occur inside pix/inst5.
inline constexpr std::array<Region, 6> kRegions{{
  {"left-attitude", 34, 146, 101, 194},
  {"fuel", 102, 153, 125, 195},
  {"radar", 134, 146, 185, 185},
  {"globe", 189, 146, 230, 194},
  {"readouts", 230, 150, 284, 195},
  {"status", 284, 158, 320, 199},
}};

struct Result {
  int samples = 0;
  int index_differences = 0;
  int rgb_differences = 0;
  int full_differences = 0;  // Neither ADF index nor its CMAP RGB matches.
  int min_x = 320, min_y = 200, max_x = -1, max_y = -1;
};

inline Result inspect(const CockpitAtlas& atlas, const uint8_t* indices,
                      const uint32_t* rgb, const Region& region) {
  Result result;
  if (!indices || !rgb) return result;
  const int x0 = region.x0 < 0 ? 0 : region.x0;
  const int y0 = region.y0 < 145 ? 145 : region.y0;
  const int x1 = region.x1 > CockpitAtlas::W ? CockpitAtlas::W : region.x1;
  const int y1 = region.y1 > CockpitAtlas::H ? CockpitAtlas::H : region.y1;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const std::size_t cell = std::size_t(y) * CockpitAtlas::W + x;
      if (!atlas.valid[cell]) continue;
      ++result.samples;
      const bool i_diff = indices[cell] != atlas.index[cell];
      const bool c_diff = (rgb[cell] & 0xffffffu) != (atlas.rgb[cell] & 0xffffffu);
      result.index_differences += i_diff;
      result.rgb_differences += c_diff;
      if (!i_diff || !c_diff) continue;
      ++result.full_differences;
      if (x < result.min_x) result.min_x = x;
      if (y < result.min_y) result.min_y = y;
      if (x > result.max_x) result.max_x = x;
      if (y > result.max_y) result.max_y = y;
    }
  }
  return result;
}

}  // namespace remaster::cockpit_dynamic_audit
