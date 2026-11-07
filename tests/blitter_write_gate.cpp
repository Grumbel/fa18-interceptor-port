#include "amiga/blitter_write_gate.hpp"

#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

int main() {
  amiga::BlitterWriteGate gate;
  std::vector<std::pair<uint32_t, uint16_t>> memory_writes;
  const auto store = [&](uint32_t addr, uint16_t word) {
    memory_writes.emplace_back(addr, word);
  };

  // All writes are forwarded by default, in order and without mutation.
  gate.write(0x1000, 0x00ff, store);
  assert(memory_writes.size() == 1);
  assert(memory_writes[0].first == 0x1000 && memory_writes[0].second == 0x00ff);
  assert(gate.scanout_word(0x1234) == 0x1234);

  // Both area and line blits use this exact gate. Suppression affects only
  // the final write; callers still perform their blit bookkeeping.
  gate.enabled = false;
  gate.write(0x1000, 0x1234, store);
  gate.write(0x1002, 0xabcd, store);
  assert(memory_writes.size() == 1);
  assert(gate.suppressed_words == 2);
  // Existing chip-RAM words are visually blanked, including data written
  // before suppression or directly by the CPU. They are not overwritten.
  const uint16_t resident_bitplane_word = 0xf00f;
  assert(gate.scanout_word(resident_bitplane_word) == 0);
  assert(resident_bitplane_word == 0xf00f);

  gate.enabled = true;
  gate.write(0x1004, 0x4321, store);
  assert(memory_writes.size() == 2);
  assert(memory_writes.back().first == 0x1004);
  assert(memory_writes.back().second == 0x4321);
  assert(gate.suppressed_words == 2);
  assert(gate.scanout_word(resident_bitplane_word) == 0xf00f);

  // Pure display isolation: hide old pixels while continuing to execute
  // all blitter writes, without affecting guest RAM or the write counter.
  gate.scanout_enabled = false;
  gate.write(0x1006, 0xabcd, store);
  assert(memory_writes.size() == 3);
  assert(memory_writes.back().first == 0x1006);
  assert(memory_writes.back().second == 0xabcd);
  assert(gate.suppressed_words == 2);
  assert(gate.scanout_word(resident_bitplane_word) == 0);
  gate.scanout_enabled = true;
  assert(gate.scanout_word(resident_bitplane_word) == 0xf00f);
}
