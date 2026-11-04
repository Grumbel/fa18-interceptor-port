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
  for (const auto& slot : kHudLetterSlots) {
    const int glyph = letter_index(slot.letter);
    assert(glyph >= 0);
    screen.fill(kBackground);
    assert(!decode_letter(screen.data(), slot, ink)); // absent
    draw(screen, slot.x, slot.y, kLetterRows[glyph]);
    assert(decode_letter(screen.data(), slot, ink) && ink == kInk);

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
  std::puts("PASS: complete HUD unit labels and numbers, partial/absent/palette fallback");
}
