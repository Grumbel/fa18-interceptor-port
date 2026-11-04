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

// Seven 3x5 numeric fields in the original 320x200 cockpit: five green
// flight/auxiliary readouts and two black-on-purple ECM numbers. The mask
// spans all three clusters but leaves uncaptured pixels untouched.
struct FlightReadouts {
  static constexpr int X = 144, Y = 158, W = 164, H = 35;
  std::array<uint32_t, W * H> rgba{};
  std::array<uint8_t, W * H> valid{};
  bool active = false;

  bool contains(int x, int y) const {
    return active && x >= X && x < X + W && y >= Y && y < Y + H &&
           valid[std::size_t(y - Y) * W + std::size_t(x - X)];
  }
  uint32_t pixel(int x, int y) const {
    return rgba[std::size_t(y - Y) * W + std::size_t(x - X)];
  }
};

// Source-font values latched when the game's HUD writer finishes drawing a
// complete glyph into an Amiga screen buffer. The presenter selects the
// displayed bank; -1 means it has no completed digit redraw for this slot.
struct HudFontCapture {
  std::array<int8_t, 12> digits{};
  std::array<uint8_t, 5> letters{};
};

// Observe the original digit writer so the displayed value tracks the
// double-buffered cockpit refresh instead of the current physics value.
void install_cockpit_instrument_hooks(amiga::Machine& m);
void reset_cockpit_instrument_state();

FuelDigits prepare_fuel_digits(amiga::Machine& m, const CockpitAtlas& cockpit,
                               const uint32_t* row_keys,
                               const uint32_t* row_palette, bool enabled);
FlightReadouts prepare_flight_readouts(amiga::Machine& m, const uint32_t* row_keys,
                                       const uint32_t* row_palette, bool enabled);
HudFontCapture captured_hud_font(const uint32_t* row_keys);
}  // namespace remaster
