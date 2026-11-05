// Amiga area-blitter fill, processed least-significant bit first.
// Both EFE and IFE are prefix-parity scans; IFE additionally includes
// the original edge bits. The incoming carry is reset at each blit row.
#pragma once

#include <cstdint>

namespace amiga {

inline uint16_t blitter_fill_word(uint16_t edges, bool& carry, bool exclusive) {
  // A parallel prefix XOR computes the fill carry after each bit in five
  // shifts instead of the former 16-iteration per-word bit loop.
  uint16_t prefix = edges;
  prefix ^= uint16_t(prefix << 1);
  prefix ^= uint16_t(prefix << 2);
  prefix ^= uint16_t(prefix << 4);
  prefix ^= uint16_t(prefix << 8);
  if (carry) prefix = uint16_t(~prefix);
  // The last prefix bit is exactly the carry leaving this 16-bit word.
  carry = (prefix & 0x8000u) != 0;
  return exclusive ? prefix : uint16_t(prefix | edges);
}

}  // namespace amiga
