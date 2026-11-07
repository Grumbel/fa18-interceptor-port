// Debug-only isolation of original Amiga blitter destination writes and
// bitplane scanout. Preserve all blitter control-flow/register effects and
// display DMA pointer advances. Suppression must be reversible without
// erasing guest RAM; keep the gate testable without SDL or the game image.
#pragma once

#include <cstdint>
#include <utility>

namespace amiga {

struct BlitterWriteGate {
  bool enabled = true;
  bool scanout_enabled = true;
  uint64_t suppressed_words = 0;

  template<class Writer>
  void write(uint32_t addr, uint16_t value, Writer&& writer) {
    if (enabled) std::forward<Writer>(writer)(addr, value);
    else ++suppressed_words;
  }

  // Blitter suppression alone leaves old art resident in chip RAM. When
  // isolating the native renderer, present zeroed bitplane words without
  // mutating that RAM; restoring the toggle restores normal scanout. The
  // independent scanout switch can hide the original picture while all
  // guest and blitter writes continue unchanged (RAM-parity diagnostic).
  uint16_t scanout_word(uint16_t original) const {
    return enabled && scanout_enabled ? original : 0;
  }
};

}  // namespace amiga
