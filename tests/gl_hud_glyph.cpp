#include "native/hud_glyph.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>

namespace {
using namespace remaster;
using namespace remaster::hud_font;

constexpr uint32_t kInk = 0x777777;
constexpr uint32_t kBackground = 0x004466;
using Screen = std::array<uint32_t, 320 * 200>;

void draw(Screen& screen, int x, int y, const uint8_t (&rows)[5], uint32_t ink = kInk) {
  for (int row = 0; row < 5; ++row)
    for (int col = 0; col < 3; ++col)
      if (rows[row] & (4 >> col))
        screen[size_t(y + row) * 320 + size_t(x + col)] = ink;
}
} // namespace

int main() {
  Screen screen{};
  screen.fill(kBackground);
  uint32_t ink = 0;
  // All HUD values can be decoded from the original writer's resident
  // glyph bytes, without reading the destination screen pixels.
  for (int number = 0; number < 10; ++number) {
    uint8_t bytes[5];
    for (int y = 0; y < 5; ++y) bytes[y] = uint8_t(kDigitRows[number][y] << 5);
    assert(source_digit(bytes) == number);
    bytes[2] ^= 0x20;
    assert(source_digit(bytes) == -1);
  }
  for (const auto& slot : kHudLetterSlots) {
    uint8_t bytes[5];
    const int glyph = letter_index(slot.letter);
    for (int y = 0; y < 5; ++y) bytes[y] = uint8_t(kLetterRows[glyph][y] << 5);
    assert(source_letter(bytes, slot.letter));
    bytes[3] ^= 0x20;
    assert(!source_letter(bytes, slot.letter));
  }
  for (const auto& slot : kHudLetterSlots) {
    const int glyph = letter_index(slot.letter);
    assert(glyph >= 0);
    screen.fill(kBackground);
    assert(!decode_letter(screen.data(), slot, ink)); // absent
    draw(screen, slot.x, slot.y, kLetterRows[glyph]);
    assert(decode_letter(screen.data(), slot, ink) && ink == kInk);
    assert(live_ink(screen.data(), slot.x, slot.y, slot.background_x,
                    kLetterRows[glyph][0], ink) && ink == kInk);

    // Any missing or newly painted pixel must retain the original bitmap.
    int first = 0;
    while (!(kLetterRows[glyph][0] & (4 >> first))) ++first;
    screen[size_t(slot.y) * 320 + size_t(slot.x + first)] = kBackground;
    assert(!decode_letter(screen.data(), slot, ink));
    screen.fill(kBackground);
    draw(screen, slot.x, slot.y, kLetterRows[glyph]);
    int blank = -1;
    for (int col = 0; col < 3 && blank < 0; ++col)
      if (!(kLetterRows[glyph][0] & (4 >> col))) blank = col;
    if (blank >= 0) {
      screen[size_t(slot.y) * 320 + size_t(slot.x + blank)] = kInk;
      assert(!decode_letter(screen.data(), slot, ink));
    }

    // All pixels must use one live grayscale pen; background-dependent
    // contrast and non-gray source shapes must not be mistaken for labels.
    screen.fill(kBackground);
    draw(screen, slot.x, slot.y, kLetterRows[glyph]);
    screen[size_t(slot.y + 1) * 320 + size_t(slot.x)] = 0x888888;
    assert(!decode_letter(screen.data(), slot, ink));
    screen.fill(kBackground);
    draw(screen, slot.x, slot.y, kLetterRows[glyph], 0x226644);
    assert(!decode_letter(screen.data(), slot, ink));
    assert(!live_ink(screen.data(), slot.x, slot.y, slot.background_x,
                     kLetterRows[glyph][0], ink));
    screen.fill(kInk);
    assert(!decode_letter(screen.data(), slot, ink));
  }

  // Shared recognition must not regress the existing numeral shapes.
  for (const auto& slot : kHudDigitSlots) {
    for (int number = 0; number < 10; ++number) {
      screen.fill(kBackground);
      draw(screen, slot.x, slot.y, kDigitRows[number]);
      assert(decode_digit(screen.data(), slot, ink) == number && ink == kInk);
    }
  }
  // The heading ticks can shift horizontally while the centre index is
  // fixed. A complete alternation of long/short ticks is required.
  auto heading = [&](int first, uint32_t colour) {
    screen.fill(kBackground);
    for (int y = 63; y <= 65; ++y) screen[size_t(y) * 320 + 159] = colour;
    for (int i = 0; i < 5; ++i) {
      const int x = first + 10 * i;
      screen[61 * 320 + size_t(x)] = colour;
      if (i % 2 == 0) screen[60 * 320 + size_t(x)] = colour;
    }
  };
  int first = 0;
  screen.fill(kBackground);
  assert(!decode_heading_marks(screen.data(), first, ink));
  for (int phase : {140, 143, 148}) {
    heading(phase, kInk);
    assert(decode_heading_marks(screen.data(), first, ink));
    assert(first == phase && ink == kInk);
    screen[61 * 320 + size_t(phase + 20)] = kBackground;
    assert(!decode_heading_marks(screen.data(), first, ink));
    heading(phase, kInk);
    screen[61 * 320 + size_t(phase + 1)] = kInk; // adjacent stray ink
    assert(!decode_heading_marks(screen.data(), first, ink));
    heading(phase, kInk);
    screen[60 * 320 + size_t(phase + 10)] = kInk; // minor became major
    assert(!decode_heading_marks(screen.data(), first, ink));
    heading(phase, kInk);
    screen[64 * 320 + 159] = 0x888888; // mixed pen in centre index
    assert(!decode_heading_marks(screen.data(), first, ink));
  }
  heading(143, 0x226644); // not the grayscale HUD palette
  assert(!decode_heading_marks(screen.data(), first, ink));
  std::puts("PASS: complete HUD numbers, labels and heading marks; partial/palette fallback");
}
