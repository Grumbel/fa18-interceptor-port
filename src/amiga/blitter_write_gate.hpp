// Debug-only isolation of original Amiga blitter destination writes.
// Preserve all blitter control-flow and register effects; only the final
// memory write is optional. Keeping this separate makes the write gate
// testable without SDL or a copy of the commercial game.
#pragma once

#include <cstdint>
#include <utility>

namespace amiga {

struct BlitterWriteGate {
  bool enabled = true;
  uint64_t suppressed_words = 0;

  template<class Writer>
  void write(uint32_t addr, uint16_t value, Writer&& writer) {
    if (enabled) std::forward<Writer>(writer)(addr, value);
    else ++suppressed_words;
  }
};

}  // namespace amiga
