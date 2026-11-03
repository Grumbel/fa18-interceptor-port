// First native dynamic cockpit instrument: the five-digit fuel LCD.
// Generated from game state, with redraws latched per original screen buffer.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace amiga { class Machine; }
namespace remaster {
struct CockpitAtlas;

struct FuelDigits {
  static constexpr int X = 104, Y = 183, W = 19, H = 5;
  std::array<uint32_t, W * H> rgba{};
  bool active = false;
  uint32_t value = 0;

  uint32_t pixel(int x, int y) const {
    return rgba[std::size_t(y - Y) * W + std::size_t(x - X)];
  }
  bool contains(int x, int y) const {
    return active && x >= X && x < X + W && y >= Y && y < Y + H;
  }
};

// Observe the original digit writer so the displayed value tracks the
// double-buffered cockpit refresh instead of the current physics value.
void install_cockpit_instrument_hooks(amiga::Machine& m);
void reset_cockpit_instrument_state();

FuelDigits prepare_fuel_digits(amiga::Machine& m, const CockpitAtlas& cockpit,
                               const uint32_t* row_keys,
                               const uint32_t* row_palette, bool enabled);
}  // namespace remaster
