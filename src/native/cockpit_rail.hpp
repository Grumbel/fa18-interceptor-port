// Native background under the HUD rails where they cross the front cockpit.
// The original ADF's pix/frnt5 contains these opaque pixels; the palette is
// the displayed Amiga copper palette, not the ILBM's initial colour map.
#pragma once

#include "native/cockpit_assets.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace remaster::cockpit_rail {

constexpr bool front_lip(int x, int y) {
  return (x == 85 || x == 233) && y >= 138 && y <= 144;
}

inline std::optional<uint32_t> asset_background(const CockpitAtlas& cockpit,
                                                  const uint32_t* row_palette,
                                                  int x, int y) {
  if (!row_palette || !front_lip(x, y)) return std::nullopt;
  const size_t cell = size_t(y) * CockpitAtlas::W + size_t(x);
  if (!cockpit.valid[cell] || cockpit.index[cell] >= 32) return std::nullopt;
  return row_palette[size_t(y) * 32 + cockpit.index[cell]];
}

}  // namespace remaster::cockpit_rail
