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
  // The GL presenter skips HUD uniform updates on high-refresh subframes
  // only when the complete captured UI state is unchanged. In particular,
  // a palette fade or a redraw in the opposite screen bank must invalidate
  // that cache even if the visible glyph masks happen to match.
  const NativeHudState blank{};
  NativeHudState state = blank;
  assert(state == blank);
  state.reticle_rgb = 0x414141;
  assert(state != blank);
  state = blank;
  state.digit_strokes[11] = 0x7f;
  assert(state != blank);
  state = blank;
  state.digit_colours[3] = 0x777777;
  assert(state != blank);
  state = blank;
  state.letter_visible[4] = 1;
  assert(state != blank);
  state = blank;
  state.letter_colours[4] = 0x666666;
  assert(state != blank);
  state = blank;
  state.heading_first_x = 150;
  assert(state != blank);
  state = blank;
  state.frame_rails = true;
  assert(state != blank);
  state = blank;
  state.pause_rgb = 0xaa6622;
  assert(state != blank);

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

  // The original point-writer capture is screen-independent. It should
  // identify a complete set even when the CPU draws it in a different order,
  // but never recognise partial or spurious points as a valid heading.
  for (int phase : {140, 143, 148}) {
    HeadingMarkWrites captured{};
    captured.add(159, 65);
    for (int i = 4; i >= 0; --i) {
      const int x = phase + 10 * i;
      captured.add(x, 61);
      if (i % 2 == 0) captured.add(x, 60);
    }
    assert(captured.phase() == -1); // centre index incomplete
    captured.add(159, 63);
    assert(captured.phase() == -1);
    captured.add(159, 64);
    assert(captured.phase() == phase);
    captured.add(phase + 1, 61); // unexpected writer command
    assert(captured.phase() == -1);

    heading(phase, kInk);
    assert(live_heading_ink(screen.data(), phase, ink) && ink == kInk);
    screen[61 * 320 + size_t(phase)] = kBackground;
    assert(!live_heading_ink(screen.data(), phase, ink));
    heading(phase, 0x226644);
    assert(!live_heading_ink(screen.data(), phase, ink));
  }
  heading(143, kInk);
  assert(!live_heading_ink(screen.data(), 137, ink));
  assert(!live_heading_ink(screen.data(), 149, ink));

  // A reticle redraw comes from two different original 68000 writers. One
  // horizontal bar is not enough, nor are four isolated vertical points.
  ReticleWrites reticle{};
  assert(!reticle.complete());
  for (int y : {71, 72, 74}) reticle.add_vertical(y);
  reticle.horizontal = true;
  assert(!reticle.complete());
  reticle.add_vertical(75);
  assert(reticle.complete());
  reticle = {};
  for (int y : {75, 74, 72, 71}) reticle.add_vertical(y);
  assert(!reticle.complete());
  reticle.horizontal = true;
  assert(reticle.complete());
  reticle.add_vertical(73); // not one of the original vertical commands
  assert(!reticle.complete());

  screen.fill(kBackground);
  screen[73 * 320 + 159] = kInk;
  assert(live_reticle_ink(screen.data(), ink) && ink == kInk);
  screen[73 * 320 + 159] = 0x111111; // faint Pause palette still visible
  assert(live_reticle_ink(screen.data(), ink) && ink == 0x111111);
  screen[73 * 320 + 159] = 0x0f0f0f;
  assert(!live_reticle_ink(screen.data(), ink));
  screen[73 * 320 + 159] = 0x776677;
  assert(!live_reticle_ink(screen.data(), ink));
  screen[73 * 320 + 159] = kBackground;
  assert(!live_reticle_ink(screen.data(), ink));

  std::puts("PASS: HUD font, heading and reticle writer capture; partial/palette fallback");
}
