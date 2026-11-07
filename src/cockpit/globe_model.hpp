// Experimental native geometry for the moving red/blue attitude globe.
// No renderer uses this yet: it deliberately has no dependency on SDL,
// OpenGL, the emulator, or the copyrighted ADF pixels. Its input mask is
// supplied by the original pix/inst5 palette indices at integration time.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cockpit::globe {

// These are the two coloured regions in the user's original pix/inst5 ILBM.
// Other ink (ticks, outline, mechanical bezel) belongs to a separate layer.
constexpr uint8_t kRedPen = 17;
constexpr uint8_t kBluePen = 21;
// Tight bounding box of pen 17/21 cells in pix/inst5, checked against the ADF.
constexpr int kLeft = 198, kRight = 221;
constexpr int kTop = 163, kBottom = 184;
constexpr double kCenterX = 209.0, kLevelHorizonY = 173.0;
constexpr int kAngleTurn = 28800;  // Original game's angle units per circle.

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

}  // namespace cockpit::globe
