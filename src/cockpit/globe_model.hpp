// Experimental native geometry for the moving red/blue attitude globe.
// The cockpit compositor can opt into this approximation, but the geometry
// itself has no dependency on SDL, OpenGL, the emulator or copyrighted ADF
// pixels. Its fill stencil comes from pix/inst5 at integration time.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cockpit::globe {

// These are the two coloured regions in the user's original pix/inst5 ILBM.
// Other ink (ticks, outline, mechanical bezel) belongs to a separate layer.
constexpr uint8_t kRedPen = 17;
constexpr uint8_t kBluePen = 21;
// During cockpit flight the displayed screen has four bitplanes: the ILBM's
// fifth bit is discarded, so the live copper pens are 1 (red) and 5 (blue).
// Verified against the original 68k replay, not inferred from the CMAP RGB.
constexpr uint8_t kFlightRedPen = 1;
constexpr uint8_t kFlightBluePen = 5;
// Tight bounding box of pen 17/21 cells in pix/inst5, checked against the ADF.
constexpr int kLeft = 198, kRight = 221;
constexpr int kTop = 163, kBottom = 184;
constexpr double kCenterX = 209.0, kLevelHorizonY = 173.0;
constexpr int kAngleTurn = 28800;  // Original game's angle units per circle.

// The original $44da4 raster helper does not take a bank angle directly.
// It reads the *projected screen-space sky polygon* copied to $60726 by
// $44b36 after the cockpit $42f62 attitude-matrix projection. Each (x,y)
// word is transformed for the compact cockpit instrument with unsigned MULU,
// LSR.L #8, ADDI.W and a signed per-frame screen offset. This gives native
// rendering a deterministic geometry input without guessing a roll sign,
// bank-angle latch or vertical pixels-per-degree conversion.
struct ProjectedPoint { int16_t x, y; };

constexpr ProjectedPoint project_sky_point(uint16_t x, uint16_t y,
                                          int16_t offset_x, int16_t offset_y) {
  return {
    int16_t(uint16_t((uint32_t(x) * 24u) >> 8) + uint16_t(193 + offset_x)),
    int16_t(uint16_t((uint32_t(y) * 31u) >> 8) + uint16_t(162 + offset_y)),
  };
}

// First experimental *integer* raster model for the usual four-vertex sky
// quad. The original 68000 places its two visible edge vertices at x=193
// and x=222, then writes a two-word-wide 32-pixel temporary bitmap. In
// recorded banked flight a word-aligned interpolation over x=192..223,
// including the upper edge row, matches 356..358 of the 358 visible ILBM
// fill cells. Do not use this approximation for other polygon layouts or
// claim it reproduces the original blitter's edge stepping in every pose.
constexpr bool supported_horizon(ProjectedPoint left, ProjectedPoint right) {
  return left.x == 193 && right.x == 222 &&
         left.y >= 140 && left.y <= 195 &&
         right.y >= 140 && right.y <= 195;
}

constexpr bool projected_sky_pixel(ProjectedPoint left, ProjectedPoint right,
                                   int x, int y) {
  // Avoid floating point and retain the same deterministic edge decision on
  // the CPU and for future high-resolution GLSL interpolation.
  return int64_t(y - left.y - 1) * 31 <
         int64_t(right.y - left.y) * (x - 192);
}

constexpr bool within_globe(int x, int y) {
  return x >= kLeft && x < kRight && y >= kTop && y < kBottom;
}

constexpr bool is_fill_pen(uint8_t pen) {
  return pen == kRedPen || pen == kBluePen;
}

// Convert a wrapped game angle to signed [-180,180) degrees. Callers will
// supply the player's pitch/bank fields once their original writer relationship
// has been verified; a screen-space offset is intentionally independent.
inline double degrees(int game_angle) {
  int a = game_angle % kAngleTurn;
  if (a < 0) a += kAngleTurn;
  if (a >= kAngleTurn / 2) a -= kAngleTurn;
  return double(a) / 80.0;
}

// Signed distance from a rotated and translated red/blue boundary. This is
// geometry in original source pixels, reusable at any OpenGL output scale.
// Positive offset moves the boundary down; bank is in native angle units.
inline double boundary_distance(double x, double y, double offset_px, int bank) {
  constexpr double tau = 6.283185307179586476925286766559;
  const double a = double(bank % kAngleTurn) * (tau / kAngleTurn);
  const double dx = x - kCenterX;
  const double dy = y - kLevelHorizonY;
  return dx * std::sin(a) + dy * std::cos(a) - offset_px;
}

// Fractional sky coverage for a high-resolution output pixel. 'scale' is
// the number of device pixels per original Amiga pixel. This is independent
// of the colour palette and can drive a GL fragment shader or a CPU test.
inline double sky_coverage(double x, double y, double offset_px, int bank,
                           double scale) {
  return std::clamp(0.5 - boundary_distance(x, y, offset_px, bank) * scale,
                    0.0, 1.0);
}

// Only recolour the asset's confirmed red/blue fill pixels. Preserve all
// mechanical frame marks and never infer opacity from a black background.
inline uint8_t palette_index(uint8_t original_pen, int x, int y,
                             double offset_px, int bank) {
  if (!within_globe(x, y) || !is_fill_pen(original_pen)) return original_pen;
  return boundary_distance(double(x), double(y), offset_px, bank) < 0
      ? kBluePen : kRedPen;
}

// For a four-plane flight framebuffer. Use its actual per-row copper colour
// for this index when painting the native layer; the ADF's RGB is only a
// reference and is different from the game's live copper palette.
inline uint8_t flight_palette_index(uint8_t original_pen, int x, int y,
                                    double offset_px, int bank) {
  if (!within_globe(x, y) || !is_fill_pen(original_pen)) return original_pen;
  return boundary_distance(double(x), double(y), offset_px, bank) < 0
      ? kFlightBluePen : kFlightRedPen;
}

}  // namespace cockpit::globe
