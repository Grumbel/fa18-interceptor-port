#include "amiga/blitter_minterm.hpp"

#include <cstdint>
#include <cstdio>

static uint16_t reference(uint8_t lf, uint16_t a, uint16_t b, uint16_t c) {
  uint16_t result = 0;
  for (int bit = 0; bit < 16; ++bit) {
    const unsigned n = ((a >> bit) & 1u) << 2 |
                       ((b >> bit) & 1u) << 1 | ((c >> bit) & 1u);
    if (lf & (1u << n)) result |= uint16_t(1u << bit);
  }
  return result;
}

int main() {
  const uint16_t patterns[] = {0, 0xffff, 0x5555, 0xaaaa, 0x00ff,
                               0xff00, 0x0123, 0x8421, 0x96a5};
  for (int lf = 0; lf < 256; ++lf)
    for (uint16_t a : patterns)
      for (uint16_t b : patterns)
        for (uint16_t c : patterns) {
          const uint16_t result = amiga::blitter_minterm(uint8_t(lf), a, b, c);
          const uint16_t expected = reference(uint8_t(lf), a, b, c);
          if (result != expected) {
            std::fprintf(stderr, "minterm %02x a=%04x b=%04x c=%04x expected=%04x got=%04x\n",
                         lf, a, b, c, expected, result);
            return 1;
          }
        }
  return 0;
}
