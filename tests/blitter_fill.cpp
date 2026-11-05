#include "amiga/blitter_fill.hpp"

#include <cstdint>
#include <cstdio>

// Independent bit-serial implementation of both Amiga fill modes.
static uint16_t reference(uint16_t edges, bool& carry, bool exclusive) {
  uint16_t result = 0;
  for (int bit = 0; bit < 16; ++bit) {
    const uint16_t mask = uint16_t(1u << bit);
    const bool edge = (edges & mask) != 0;
    if (exclusive) {
      carry ^= edge;
      if (carry) result |= mask;
    } else {
      if (carry || edge) result |= mask;
      carry ^= edge;
    }
  }
  return result;
}

int main() {
  for (int exclusive = 0; exclusive < 2; ++exclusive) {
    for (int initial = 0; initial < 2; ++initial) {
      for (uint32_t word = 0; word < 65536; ++word) {
        bool old_carry = initial != 0, new_carry = old_carry;
        const uint16_t expected = reference(uint16_t(word), old_carry, exclusive != 0);
        const uint16_t actual = amiga::blitter_fill_word(uint16_t(word), new_carry,
                                                       exclusive != 0);
        if (actual != expected || old_carry != new_carry) {
          std::fprintf(stderr, "fill mismatch mode=%d carry=%d word=%04x expected=%04x actual=%04x\n",
                       exclusive, initial, unsigned(word), expected, actual);
          return 1;
        }
      }
    }
  }
  // The carry from one word feeds the next, but every new row resets it.
  for (int exclusive = 0; exclusive < 2; ++exclusive) {
    bool old_carry = true, new_carry = true;
    for (uint32_t w = 0; w < 10000; ++w) {
      const uint16_t edges = uint16_t(w * 28771u + 0x1357u);
      if (reference(edges, old_carry, exclusive != 0) !=
          amiga::blitter_fill_word(edges, new_carry, exclusive != 0) ||
          old_carry != new_carry) return 2;
    }
  }
  return 0;
}
