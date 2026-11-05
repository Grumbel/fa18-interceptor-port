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
  std::array<uint32_t, H> background_rgba{};
  std::array<uint8_t, 5> stroke_masks{};
  bool active = false;
  bool vector_enabled = false;
  uint32_t value = 0;
  uint32_t ink_rgb = 0;

  uint32_t pixel(int x, int y) const {
    return rgba[std::size_t(y - Y) * W + std::size_t(x - X)];
  }
  uint32_t backing(int x, int y) const {
    return vector_enabled ? background_rgba[std::size_t(y - Y)] : pixel(x, y);
  }
  bool contains(int x, int y) const {
    return active && x >= X && x < X + W && y >= Y && y < Y + H;
  }
};

// The adjacent vertical gauge is 4x23 source pixels, filled from the bottom.
// Its height is derived from the *displayed* fuel bank, not the current
// physics tick. At enhanced resolution the bar is drawn as a native shape;
// its background and ink use the live copper colours.
struct FuelBar {
  static constexpr int X = 111, Y = 158, W = 4, H = 23;
  std::array<uint32_t, H> background_rgba{};
  uint32_t ink_rgb = 0;
  int filled_rows = 0;
  bool active = false;

  bool contains(int x, int y) const {
    return active && x >= X && x < X + W && y >= Y && y < Y + H;
  }
  uint32_t backing(int y) const {
    return background_rgba[std::size_t(y - Y)];
  }
};

// Small moving symbols on the central radar screen. The two diagonal guides
// and centre reference are native geometry. The contact's position comes
// from the original pen-5 pixel-writer hook, latched separately for each
// Amiga screen buffer; its colour comes from the copper palette. The original
// guide stencil and contact's single-pixel visibility gate protect menus,
// Pause and intermediate double-buffer redraws.
struct RadarSymbols {
  static constexpr int X = 135, Y = 146, W = 47, H = 36;
  bool active = false;
  int dot_x = -1, dot_y = -1;
  uint32_t guide_rgb = 0, marker_rgb = 0, dot_rgb = 0;
  std::array<uint32_t, H> background{};

  static bool guide_at(int x, int y) {
    return y >= 148 && y <= 163 &&
           (x == 139 + y - 148 || x == 177 - (y - 148));
  }
  static bool marker_at(int x, int y) {
    return (x == 158 && y == 167) ||
           (y == 168 && x >= 157 && x <= 159);
  }
  bool source_at(int x, int y, uint32_t rgb) const {
    if (!active) return false;
    const uint32_t c = rgb & 0xffffffu;
    return (guide_at(x, y) && c == guide_rgb) ||
           (marker_at(x, y) && c == marker_rgb) ||
           (x == dot_x && y == dot_y && c == dot_rgb);
  }
  uint32_t backing(int y) const {
    return background[std::size_t(y - Y)];
  }
};

// Seven 3x5 numeric fields in the original 320x200 cockpit: five green
// flight/auxiliary readouts and two black-on-purple ECM numbers. The mask
// spans all three clusters but leaves uncaptured pixels untouched.
struct FlightReadouts {
  static constexpr int X = 144, Y = 158, W = 164, H = 35;
  struct VectorDigit {
    uint16_t x = 0, y = 0;
    uint8_t strokes = 0;
    uint8_t ink_pen = 0, background_pen = 0;
  };
  std::array<uint32_t, W * H> rgba{};
  std::array<uint8_t, W * H> valid{};
  // A one-based index identifies only *complete*, recognised source-font
  // redraws; partial digits retain the original 3x5 pixel path.
  std::array<uint8_t, W * H> vector_index{};
  std::array<VectorDigit, 64> vectors{};
  uint8_t vector_count = 0;
  bool active = false;

  bool contains(int x, int y) const {
    return active && x >= X && x < X + W && y >= Y && y < Y + H &&
           valid[std::size_t(y - Y) * W + std::size_t(x - X)];
  }
  uint32_t pixel(int x, int y) const {
    return rgba[std::size_t(y - Y) * W + std::size_t(x - X)];
  }
  const VectorDigit* digit(int x, int y) const {
    const auto i = vector_index[std::size_t(y - Y) * W + std::size_t(x - X)];
    return i ? &vectors[std::size_t(i - 1)] : nullptr;
  }
  uint32_t backing(int x, int y, const uint32_t* palette) const {
    if (const VectorDigit* v = digit(x, y))
      return palette[std::size_t(y) * 32 + v->background_pen];
    return pixel(x, y);
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
FuelBar prepare_fuel_bar(const FuelDigits& displayed_fuel,
                         const uint32_t* row_palette, int hires_scale);
RadarSymbols prepare_radar_symbols(amiga::Machine& m, const uint32_t* frame_rgb,
                                   const uint32_t* row_keys, const uint32_t* row_palette,
                                   bool enabled);
FlightReadouts prepare_flight_readouts(amiga::Machine& m, const uint32_t* row_keys,
                                       const uint32_t* row_palette, bool enabled);
HudFontCapture captured_hud_font(const uint32_t* row_keys);
// -1 when the active bank has not completed a recognisable compass redraw.
int captured_heading_phase(const uint32_t* row_keys);
// True after both original reticle drawing operations completed in this bank.
bool captured_reticle_ready(const uint32_t* row_keys);
}  // namespace remaster
