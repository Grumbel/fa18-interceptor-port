// Source-screen 3x5 HUD glyph recognition. This is deliberately independent
// of the Amiga renderer so that a partial redraw cannot turn into a native
// stroke unless every source pixel matches the expected glyph and ink.
#pragma once

#include "native/remaster.hpp"

#include <algorithm>
#include <cstdint>

namespace remaster::hud_font {

inline constexpr int kScreenWidth = 320;
inline constexpr uint8_t kDigitRows[10][5] = {
    {7, 5, 5, 5, 7}, {2, 2, 2, 2, 2}, {7, 1, 7, 4, 7},
    {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7},
    {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7},
    {7, 5, 7, 1, 1},
};
inline constexpr uint8_t kLetterRows[4][5] = {
    {5, 5, 6, 5, 5}, // K: source glyphs use single diagonal pixels
    {7, 2, 2, 2, 2}, // T
    {7, 4, 7, 4, 4}, // F
    {3, 4, 5, 5, 3}, // G
};
// Strokes A,B,C,D,E,F,G (1 is a centre stem, not a right-side seven).
inline constexpr uint8_t kDigitStrokes[10] = {63, 128, 91, 79, 102, 109, 125, 7, 127, 103};

inline int letter_index(char letter) {
  switch (letter) {
    case 'K': return 0;
    case 'T': return 1;
    case 'F': return 2;
    case 'G': return 3;
    default: return -1;
  }
}

inline bool glyph_ink(uint32_t pixel, uint32_t background) {
  const int r = int((pixel >> 16) & 255);
  const int g = int((pixel >> 8) & 255);
  const int b = int(pixel & 255);
  return r == g && g == b && r >= 24 &&
         r > int((background >> 16) & 255) + 12 &&
         g > int((background >> 8) & 255) + 12;
}

inline bool read_stencil(const uint32_t* rgb, int x0, int y0, int background_x,
                         uint8_t (&rows)[5], uint32_t& ink) {
  ink = 0;
  for (int y = 0; y < 5; ++y) {
    rows[y] = 0;
    const uint32_t bg = rgb[(y0 + y) * kScreenWidth + background_x];
    for (int x = 0; x < 3; ++x) {
      const uint32_t colour = rgb[(y0 + y) * kScreenWidth + x0 + x];
      if (glyph_ink(colour, bg)) {
        rows[y] |= uint8_t(4 >> x);
        if (!ink) ink = colour & 0xffffffu;
        else if ((colour & 0xffffffu) != ink) return false;
      }
    }
  }
  return ink != 0;
}

inline int decode_digit(const uint32_t* rgb, const HudDigitSlot& slot, uint32_t& ink) {
  uint8_t rows[5]{};
  if (!read_stencil(rgb, slot.x, slot.y, slot.background_x, rows, ink)) return -1;
  for (int digit = 0; digit < 10; ++digit)
    if (std::equal(rows, rows + 5, kDigitRows[digit])) return digit;
  return -1;
}

inline bool decode_letter(const uint32_t* rgb, const HudLetterSlot& slot, uint32_t& ink) {
  uint8_t rows[5]{};
  const int glyph = letter_index(slot.letter);
  return glyph >= 0 &&
         read_stencil(rgb, slot.x, slot.y, slot.background_x, rows, ink) &&
         std::equal(rows, rows + 5, kLetterRows[glyph]);
}

} // namespace remaster::hud_font
