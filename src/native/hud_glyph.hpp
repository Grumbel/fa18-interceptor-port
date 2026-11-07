// Source-screen 3x5 HUD glyph recognition. This is deliberately independent
// of the Amiga renderer so that a partial redraw cannot turn into a native
// stroke unless every source pixel matches the expected glyph and ink.
#pragma once

#include "native/remaster.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
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

// Each source-screen cell belongs to at most one native HUD digit or label.
// Populate this once, then perform two O(1) lookups per compositor pixel
// instead of rescanning all seventeen 3x5 slots at 120/240 Hz.
struct SourceTextCells {
  std::array<int8_t, 320 * 200> digits{};
  std::array<int8_t, 320 * 200> labels{};

  SourceTextCells() {
    digits.fill(-1);
    labels.fill(-1);
    for (size_t i = 0; i < kHudDigitSlots.size(); ++i) {
      const auto& slot = kHudDigitSlots[i];
      for (int y = slot.y; y < slot.y + 5; ++y)
        for (int x = slot.x; x < slot.x + 3; ++x)
          digits[size_t(y) * 320 + x] = int8_t(i);
    }
    for (size_t i = 0; i < kHudLetterSlots.size(); ++i) {
      const auto& slot = kHudLetterSlots[i];
      for (int y = slot.y; y < slot.y + 5; ++y)
        for (int x = slot.x; x < slot.x + 3; ++x)
          labels[size_t(y) * 320 + x] = int8_t(i);
    }
  }

  int digit_at(int x, int y) const {
    return x >= 0 && x < 320 && y >= 0 && y < 200 ?
        digits[size_t(y) * 320 + x] : -1;
  }
  int label_at(int x, int y) const {
    return x >= 0 && x < 320 && y >= 0 && y < 200 ?
        labels[size_t(y) * 320 + x] : -1;
  }
};

inline const SourceTextCells& source_text_cells() {
  static const SourceTextCells cells;
  return cells;
}

// The exact original 3x5 font footprint, used for clearing the old writer's
// ink under native strokes. Zero value_plus_one denotes an unobserved bank.
constexpr bool writer_digit_pixel(uint8_t value_plus_one, int x, int y) {
  return value_plus_one >= 1 && value_plus_one <= 10 &&
         x >= 0 && x < 3 && y >= 0 && y < 5 &&
         (kDigitRows[value_plus_one - 1][y] & (4 >> x)) != 0;
}

inline int letter_index(char letter) {
  switch (letter) {
    case 'K': return 0;
    case 'T': return 1;
    case 'F': return 2;
    case 'G': return 3;
    default: return -1;
  }
}

inline bool writer_letter_pixel(char letter, int x, int y) {
  const int glyph = letter_index(letter);
  return glyph >= 0 && x >= 0 && x < 3 && y >= 0 && y < 5 &&
         (kLetterRows[glyph][y] & (4 >> x)) != 0;
}

// The 68000 HUD writer receives five consecutive glyph bytes, with the
// three meaningful bits in the high end of each byte. Decode those source
// bytes directly, independent of display-plane pixels or palette fades.
inline int source_digit(const uint8_t (&bytes)[5]) {
  for (int n = 0; n < 10; ++n) {
    bool same = true;
    for (int y = 0; y < 5; ++y)
      same &= (bytes[y] >> 5) == kDigitRows[n][y];
    if (same) return n;
  }
  return -1;
}

inline bool source_letter(const uint8_t (&bytes)[5], char letter) {
  const int glyph = letter_index(letter);
  if (glyph < 0) return false;
  for (int y = 0; y < 5; ++y)
    if ((bytes[y] >> 5) != kLetterRows[glyph][y]) return false;
  return true;
}

inline bool glyph_ink(uint32_t pixel, uint32_t background) {
  const int r = int((pixel >> 16) & 255);
  const int g = int((pixel >> 8) & 255);
  const int b = int(pixel & 255);
  return r == g && g == b && r >= 24 &&
         r > int((background >> 16) & 255) + 12 &&
         g > int((background >> 8) & 255) + 12;
}

// Reference path for an original 68000 font-writer capture: an expected ink
// pixel supplies the copper-faded colour and confirms glyph visibility.
// The palette-backed path uses pen 13 without reading this source pixel.
inline bool live_ink(const uint32_t* rgb, int x, int y, int background_x,
                     uint8_t first_row, uint32_t& ink) {
  for (int col = 0; col < 3; ++col) {
    if (!(first_row & (4 >> col))) continue;
    const uint32_t pixel = rgb[y * kScreenWidth + x + col];
    if (!glyph_ink(pixel, rgb[y * kScreenWidth + background_x])) return false;
    ink = pixel & 0xffffffu;
    return true;
  }
  return false;
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

// The original HUD's $444c0 one-pixel writer emits the compass tick and
// centre-index points individually. Accumulate *write commands* for one
// redraw, rather than looking at the completed screen bitmap. An exact
// eleven-point set identifies its moving phase; partial/unfamiliar sets
// have no native replacement yet.
struct HeadingMarkWrites {
  uint64_t row60 = 0, row61 = 0;
  uint8_t centre = 0;

  void add(int x, int y) {
    if (x == 159 && y >= 63 && y <= 65)
      centre |= uint8_t(1u << (y - 63));
    else if (x >= 138 && x <= 188 && (y == 60 || y == 61)) {
      const uint64_t bit = uint64_t(1) << (x - 128);
      if (y == 60) row60 |= bit;
      else row61 |= bit;
    }
  }

  int phase() const {
    if (centre != 7) return -1;
    for (int start = 138; start <= 148; ++start) {
      uint64_t expected60 = 0, expected61 = 0;
      for (int i = 0; i < 5; ++i) {
        const uint64_t bit = uint64_t(1) << (start + 10 * i - 128);
        expected61 |= bit;
        if ((i % 2) == 0) expected60 |= bit;
      }
      if (row60 == expected60 && row61 == expected61) return start;
    }
    return -1;
  }
};

// Once the writer has supplied the phase, the original screen is needed
// only for the currently faded palette colour and a minimal visibility
// guard. No tick spacing or shape is inferred from these pixels.
inline bool live_heading_ink(const uint32_t* rgb, int first_x, uint32_t& ink) {
  if (first_x < 138 || first_x > 148) return false;
  const uint32_t index = rgb[63 * kScreenWidth + 159] & 0xffffffu;
  if (!glyph_ink(index, rgb[63 * kScreenWidth + 165]) ||
      (rgb[61 * kScreenWidth + first_x] & 0xffffffu) != index) return false;
  ink = index;
  return true;
}

// The original reticle is a five-pixel horizontal row plus four vertical
// points (the centre belongs to the row). These are two separate 68000
// drawing operations. Require both parts in one redraw before enabling the
// native vector cross, independent of the destination bitmap's 5x5 stencil.
struct ReticleWrites {
  uint8_t vertical_rows = 0;
  bool horizontal = false;

  void add_vertical(int y) {
    if (y >= 71 && y <= 75)
      vertical_rows |= uint8_t(1u << (y - 71));
  }
  bool complete() const { return horizontal && vertical_rows == 0x1b; }
};

inline bool live_reticle_ink(const uint32_t* rgb, uint32_t& ink) {
  const uint32_t pixel = rgb[73 * kScreenWidth + 159] & 0xffffffu;
  const uint32_t backdrop = rgb[73 * kScreenWidth + 165] & 0xffffffu;
  const uint8_t r = uint8_t(pixel >> 16);
  if (pixel == backdrop || r < 16 || pixel != uint32_t(r) * 0x010101u)
    return false;
  ink = pixel;
  return true;
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
