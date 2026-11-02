// FA-18's original 5x7 screen/menu font, loaded from the user's ADF.
// The font bitmap is copyrighted game data and is never built into this port.
#pragma once

#include "amiga/hunk.hpp"
#include "font6x10.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace original_menu_font {

constexpr int W = 5;
constexpr int H = 7;
constexpr int ADVANCE = 7;  // original Amiga menu has two empty columns
constexpr int GLYPHS = 53;
constexpr int HUNK = 61;
constexpr size_t OFFSET = 0x1f3;
constexpr size_t TABLE_BYTES = GLYPHS * H;

// Hunk 61 contains a compact, non-ASCII-ordered table: punctuation,
// digits 0-9, colon, then A-Z. The game displays capitals for menu text.
constexpr int glyph_index(char ch) {
  unsigned char c = static_cast<unsigned char>(ch);
  if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - 'a' + 'A');
  if (c >= '0' && c <= '9') return 16 + int(c - '0');
  if (c >= 'A' && c <= 'Z') return 27 + int(c - 'A');
  switch (c) {
    case '!': return 0;
    case '*': return 2;
    case '@': return 3;
    case '#': return 4;
    case '"': return 5;
    case '%': return 6;
    case '\'': return 7;
    case ',': return 8;
    case '-': return 9;
    case '.': return 10;
    case '(': return 11;
    case ')': return 12;
    case '/': return 13;
    case ':': return 26;
    default: return -1;
  }
}

inline uint32_t fnv1a(const uint8_t* data, size_t length) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < length; ++i) {
    h ^= data[i];
    h *= 16777619u;
  }
  return h;
}

// The disk table doesn't have these modern host-menu punctuation marks.
// Supply simple 5x7 strokes rather than mixing in the taller fallback font.
constexpr uint8_t extra_symbol(char ch, int y) {
  constexpr uint8_t gt[H] = {0x80, 0x40, 0x20, 0x10, 0x20, 0x40, 0x80};
  constexpr uint8_t lt[H] = {0x08, 0x10, 0x20, 0x40, 0x20, 0x10, 0x08};
  constexpr uint8_t plus[H] = {0x00, 0x20, 0x20, 0xf8, 0x20, 0x20, 0x00};
  constexpr uint8_t equals[H] = {0x00, 0x00, 0xf8, 0x00, 0xf8, 0x00, 0x00};
  constexpr uint8_t open[H] = {0x70, 0x40, 0x40, 0x40, 0x40, 0x40, 0x70};
  constexpr uint8_t close[H] = {0x70, 0x10, 0x10, 0x10, 0x10, 0x10, 0x70};
  switch (ch) {
    case '>': return gt[y];
    case '<': return lt[y];
    case '+': return plus[y];
    case '=': return equals[y];
    case '[': return open[y];
    case ']': return close[y];
    default: return 0;
  }
}

class Font {
 public:
  bool load_executable(const std::vector<uint8_t>& exe) {
    valid_ = false;
    amiga::HunkFile file;
    if (!file.parse(exe) || file.hunks.size() <= HUNK) return false;
    const amiga::Hunk& hunk = file.hunks[HUNK];
    if (hunk.kind != amiga::Hunk::Code || hunk.data.size() < OFFSET + TABLE_BYTES)
      return false;
    const uint8_t* src = hunk.data.data() + OFFSET;
    // Recognize the expected original font, not arbitrary bytes at this
    // address in an unrelated or modified executable. No font bytes embedded.
    if (fnv1a(src, TABLE_BYTES) != 0x333527efu) return false;
    std::copy(src, src + TABLE_BYTES, rows_.begin());
    valid_ = true;
    return true;
  }

  bool valid() const { return valid_; }

  // ARGB8888 target. Rendering takes place at integer magnification, like
  // the rest of the host's menu overlay; there is no font smoothing.
  void draw(std::vector<uint32_t>& canvas, int width, int height,
            int x, int y, std::string_view text, int scale, uint32_t colour) const {
    if (!valid_ || width <= 0 || height <= 0 || scale <= 0 ||
        canvas.size() < size_t(width) * size_t(height)) return;
    for (size_t i = 0; i < text.size(); ++i) {
      char ch = text[i];
      int index = glyph_index(ch);
      if (ch == ' ') continue;
      for (int gy = 0; gy < H; ++gy) {
        uint8_t bits = 0;
        if (index >= 0) {
          bits = rows_[size_t(index) * H + size_t(gy)];
        } else {
          // A few host-only control labels use punctuation not present in
          // the game's compact table. Sample our existing ASCII font for
          // those characters, retaining the original 5x7 spacing.
          unsigned char c = static_cast<unsigned char>(ch);
          if (c < 32 || c > 126) c = '?';
          bits = extra_symbol(ch, gy);
          if (bits == 0 && ch != '>' && ch != '<' && ch != '+' &&
              ch != '=' && ch != '[' && ch != ']') {
            bits = static_cast<uint8_t>(
              font6x10::glyphs[(size_t(c) - 32) * font6x10::H + size_t(gy + 3)] << 2);
          }
        }
        for (int gx = 0; gx < W; ++gx) {
          if (!(bits & (0x80u >> gx))) continue;
          for (int yy = 0; yy < scale; ++yy)
            for (int xx = 0; xx < scale; ++xx) {
              int px = x + (int(i) * ADVANCE + gx) * scale + xx;
              int py = y + gy * scale + yy;
              if (px >= 0 && px < width && py >= 0 && py < height)
                canvas[size_t(py) * size_t(width) + size_t(px)] = colour;
            }
        }
      }
    }
  }

 private:
  bool valid_ = false;
  std::array<uint8_t, TABLE_BYTES> rows_{};
};

}  // namespace original_menu_font
