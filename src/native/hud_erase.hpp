// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace remaster::hud_erase {

// Footprints of the original 320x200 blitter/point-writer commands. These
// coordinates come from the captured writers, not the displayed bitmap.
constexpr bool frame_crown(int x, int y) {
  return (y == 37 && x >= 93 && x <= 225) ||
         (y >= 38 && y <= 45 && (x == 130 - y || x == 188 + y));
}

constexpr bool frame_rail(int x, int y) {
  return y >= 46 && y <= 144 && (x == 85 || x == 233);
}

constexpr bool reticle(int x, int y) {
  return (x == 159 && y >= 71 && y <= 75) ||
         (y == 73 && x >= 157 && x <= 161);
}

// For a completed writer capture, the original raster ink need not be
// inspected to remove its footprint. Unknown/partial screen banks still use
// the previous colour-matched bitmap stencil. The caller separately gates
// visible HUD regions and controls the bitmap-reference switch.
constexpr bool erase_pixel(bool writer_capture, bool native_erase,
                           uint32_t source_rgb, uint32_t ink_rgb) {
  return (native_erase && writer_capture) ||
         ((source_rgb & 0xffffffu) == (ink_rgb & 0xffffffu));
}

}  // namespace remaster::hud_erase
