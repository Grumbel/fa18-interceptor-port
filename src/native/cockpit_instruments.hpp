// First native dynamic cockpit instrument: the five-digit fuel LCD.
// Generated from game state, with source-image parity as an incremental gate.
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

FuelDigits prepare_fuel_digits(amiga::Machine& m, const CockpitAtlas& cockpit,
                               const uint8_t* screen_index, const uint32_t* screen_rgb,
                               const uint32_t* row_palette, bool enabled);
}  // namespace remaster
