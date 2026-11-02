#include "original_menu_font.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
  using namespace original_menu_font;
  assert(glyph_index('0') == 16);
  assert(glyph_index('9') == 25);
  assert(glyph_index(':') == 26);
  assert(glyph_index('A') == 27);
  assert(glyph_index('z') == 52);
  assert(glyph_index(' ') == -1);
  assert(glyph_index('>') == -1);
  assert(extra_symbol('>', 3) == 0x10);
  assert(extra_symbol('+', 3) == 0xf8);
  assert(extra_symbol('[', 0) == 0x70);
  assert(extra_symbol('=', 2) == 0xf8);

  const uint8_t abc[] = {'a', 'b', 'c'};
  assert(fnv1a(abc, sizeof abc) == 0x1a47e90bu);

  // The parser must reject an absent or unrelated executable instead of
  // trying to render arbitrary Hunk-61 bytes as font data.
  Font font;
  assert(!font.load_executable({}));
  assert(!font.valid());
  std::vector<uint32_t> canvas(100, 0xdeadbeefu);
  font.draw(canvas, 10, 10, 0, 0, "TEST", 1, 0xffffffffu);
  for (uint32_t pixel : canvas) assert(pixel == 0xdeadbeefu);
}
