// First native dynamic cockpit instrument: the five-digit fuel LCD.
// Generated from game state, with redraws latched per original screen buffer.
#pragma once

#include "cockpit/globe_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

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

// The radar's diagonal guides, inverted-T reference and moving contact are
// captured from the original blitter/CPU writers per Amiga screen buffer.
// The enhanced shapes and colours need no source bitmap lookup; a diagnostic
// fallback can still check the legacy source stencil for A/B comparison.
struct RadarSymbols {
  static constexpr int X = 135, Y = 146, W = 47, H = 36;
  bool active = false;
  int dot_x = -1, dot_y = -1;
  int guide_y = 148;
  int marker_x = 158, marker_y = 167;
  bool bitmap_reference = false;
  uint32_t guide_rgb = 0, marker_rgb = 0, dot_rgb = 0;
  std::array<uint32_t, H> background{};

  bool guide_at(int x, int y) const {
    return y >= guide_y && y <= guide_y + 15 &&
           (x == 139 + y - guide_y || x == 177 - (y - guide_y));
  }
  bool marker_at(int x, int y) const {
    return (x == marker_x && y == marker_y) ||
           (y == marker_y + 1 && x >= marker_x - 1 && x <= marker_x + 1);
  }
  bool source_at(int x, int y, uint32_t rgb) const {
    if (!active) return false;
    const bool guide = guide_at(x, y), marker = marker_at(x, y);
    const bool dot = x == dot_x && y == dot_y;
    if (!bitmap_reference) return guide || marker || dot;
    const uint32_t c = rgb & 0xffffffu;
    return (guide && c == guide_rgb) ||
           (marker && c == marker_rgb) || (dot && c == dot_rgb);
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

// The *original 68000* has already projected the sky quad into instrument
// pixels. This is latched independently for each cockpit framebuffer, then
// selected using the actually displayed row pointer. It is available only
// for the supported four-vertex, two-word raster layout; unknown geometry
// continues through the original Amiga bitmap path.
struct CapturedGlobe {
  std::array<cockpit::globe::ProjectedPoint, 4> vertices{};
  int16_t offset_x = 0, offset_y = 0;
  bool inverted = false;
  bool vertical = false;
  bool active = false;
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
CapturedGlobe captured_globe(const uint32_t* row_keys);
std::optional<uint8_t> projected_globe_pen(const CapturedGlobe& globe,
                                            const CockpitAtlas& cockpit,
                                            int x, int y);
// -1 when the active bank has not completed a recognisable compass redraw.
int captured_heading_phase(const uint32_t* row_keys);
// True after both original reticle drawing operations completed in this bank.
bool captured_reticle_ready(const uint32_t* row_keys);
// Frame of the last complete five-line HUD outline write in the displayed
// bank; UINT64_MAX means no verified original frame command sequence yet.
uint64_t captured_hud_frame(const uint32_t* row_keys);
// Distinct original 5x7 Pause-heading ink pixels drawn in the displayed bank.
// The displayed title lags the writer by one Amiga frame; zero before it starts.
int captured_pause_title_pixels(const uint32_t* row_keys, uint64_t display_frame);
// Drop title captures after leaving Pause (including rapid pause/resume).
void observe_pause_title_state(bool paused);
}  // namespace remaster
