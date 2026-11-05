// Amiga blitter's three-input Boolean function. BLTCON0 low byte encodes
// the truth table in A,B,C order, with C varying fastest.
#pragma once

#include <cstdint>

namespace amiga {

inline uint16_t blitter_minterm(uint8_t lf, uint16_t a, uint16_t b, uint16_t c) {
  // Ordered by observed scene word volume. The frequent operations take
  // a small number of predictable tests, with no inner-loop jump table.
  if (lf == 0x00) return 0;
  if (lf == 0x0c) return uint16_t(~a & b);
  if (lf == 0xf0) return a;
  if (lf == 0xfc) return uint16_t(a | b);
  if (lf == 0xfa) return uint16_t(a | c);
  if (lf == 0x4a) return uint16_t((~a & c) | (a & b & ~c));
  if (lf == 0x0a) return uint16_t(~a & c);
  uint16_t r = 0;
  if (lf & 0x01) r |= ~a & ~b & ~c;
  if (lf & 0x02) r |= ~a & ~b & c;
  if (lf & 0x04) r |= ~a & b & ~c;
  if (lf & 0x08) r |= ~a & b & c;
  if (lf & 0x10) r |= a & ~b & ~c;
  if (lf & 0x20) r |= a & ~b & c;
  if (lf & 0x40) r |= a & b & ~c;
  if (lf & 0x80) r |= a & b & c;
  return r;
}

}  // namespace amiga
