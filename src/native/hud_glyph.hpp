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

// The heading tape has five 10-column-spaced ticks, with alternating
// 2-pixel/1-pixel heights. Its phase can move with the aircraft's heading;
// the fixed three-pixel centre index remains at x=159. Accept only the
// complete 8+3-pixel group, in one live pen colour, before replacing it.
inline bool decode_heading_marks(const uint32_t* rgb, int& first_x, uint32_t& ink) {
  constexpr int index_x = 159;
  uint32_t index_ink = rgb[63 * kScreenWidth + index_x] & 0xffffffu;
  if (!glyph_ink(index_ink, rgb[63 * kScreenWidth + 165]) ||
      (rgb[64 * kScreenWidth + index_x] & 0xffffffu) != index_ink ||
      (rgb[65 * kScreenWidth + index_x] & 0xffffffu) != index_ink ||
      (rgb[62 * kScreenWidth + index_x] & 0xffffffu) == index_ink ||
      (rgb[66 * kScreenWidth + index_x] & 0xffffffu) == index_ink) return false;
  for (int start = 138; start <= 148; ++start) {
    bool matches = true;
    for (int i = 0; i < 5 && matches; ++i) {
      const int x = start + i * 10;
      const uint32_t pixel = rgb[61 * kScreenWidth + x] & 0xffffffu;
      matches = pixel == index_ink &&
                glyph_ink(pixel, rgb[61 * kScreenWidth + x + 2]) &&
                (rgb[61 * kScreenWidth + x - 1] & 0xffffffu) != index_ink &&
                (rgb[61 * kScreenWidth + x + 1] & 0xffffffu) != index_ink;
      const bool major = (i % 2) == 0;
      const uint32_t upper = rgb[60 * kScreenWidth + x] & 0xffffffu;
      matches = matches && (major ? upper == index_ink &&
                            glyph_ink(upper, rgb[60 * kScreenWidth + x + 2]) :
                            upper != index_ink);
    }
    if (matches) {
      first_x = start;
      ink = index_ink;
      return true;
    }
  }
  return false;
}

} // namespace remaster::hud_font
