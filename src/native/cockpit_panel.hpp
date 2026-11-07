// First framebuffer-independent portion of the lower pix/inst5 cockpit.
// The ADF image is opaque here; unlike frnt5, its black cells are painted.
// Keep the moving attitude display, radar, LCDs, switches and their nearby
// labels in the original 2D overlay until their writers are reconstructed.
#pragma once

#include "native/cockpit_assets.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace remaster::cockpit_panel {

constexpr int Y = 145, H = 55;

// Only parts of the panel where the original image contains fixed structural
// artwork. Do not infer static pixels from their current framebuffer colour:
// the masks are independent of double-buffered bitplane contents.
inline bool structural(int x, int y) {
  if (x < 0 || x >= CockpitAtlas::W || y < Y || y >= Y + H) return false;
  return y == Y || y == Y + H - 1 || x < 4 ||
         (x >= 125 && x < 133) || (x >= 190 && x < 227) || x >= 311;
}

inline bool painted(const CockpitAtlas& atlas, int x, int y) {
  if (!structural(x, y)) return false;
  const std::size_t cell = std::size_t(y) * CockpitAtlas::W + std::size_t(x);
  return atlas.valid[cell] && atlas.index[cell] < 32;
}

// Require a completed HUD outline and an independently observed fuel writer
// *for the currently displayed screen bank*. This avoids resurrecting stale
// cockpit artwork in external views, partial redraws and screen transitions.
inline bool enabled(bool hud_from_writer, bool hud_rails, bool fuel_from_writer) {
  return hud_from_writer && hud_rails && fuel_from_writer;
}

inline std::optional<uint32_t> pixel(const CockpitAtlas& atlas,
                                     const uint32_t* palette, int x, int y) {
  if (!palette || !painted(atlas, x, y)) return std::nullopt;
  const std::size_t cell = std::size_t(y) * CockpitAtlas::W + std::size_t(x);
  return palette[std::size_t(y) * 32 + atlas.index[cell]];
}

}  // namespace remaster::cockpit_panel
