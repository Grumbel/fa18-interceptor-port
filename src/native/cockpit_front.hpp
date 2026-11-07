// Native front-cockpit silhouette from pix/frnt5. The artwork is embedded in
// the user's ADF, never in the port. Only its *painted*, nonblack cells are
// independent of the 3D scene behind it; the ILBM's black cells are not an
// opacity mask and may leave the original sky visible.
#pragma once

#include "native/cockpit_assets.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace remaster::cockpit_front {

constexpr int X = 16, Y = 133, W = 288, H = 12;

inline bool painted(const CockpitAtlas& atlas, int x, int y) {
  if (x < X || x >= X + W || y < Y || y >= Y + H) return false;
  const std::size_t cell = std::size_t(y) * CockpitAtlas::W + std::size_t(x);
  return atlas.valid[cell] && (atlas.rgb[cell] & 0xffffffu) != 0 &&
         atlas.index[cell] < 32;
}

// Resolve the ADF's original pen against the displayed *row* copper palette.
// Calling code must independently establish that the front cockpit is drawn
// in this Amiga screen bank (the complete HUD-frame writer latch).
inline std::optional<uint32_t> pixel(const CockpitAtlas& atlas,
                                     const uint32_t* palette, int x, int y) {
  if (!palette || !painted(atlas, x, y)) return std::nullopt;
  const std::size_t cell = std::size_t(y) * CockpitAtlas::W + std::size_t(x);
  return palette[std::size_t(y) * 32 + atlas.index[cell]];
}

}  // namespace remaster::cockpit_front
