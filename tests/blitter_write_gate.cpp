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

  // Region masks operate on already-fetched palette indices, so the DMA
  // stream and all guest writes remain identical while selected instruments
  // disappear. Half-open boundaries are defined in 320x200 source space.
  gate.hidden_region = amiga::ScanoutRegion::Globe;
  assert(gate.scanout_pixel(0x15, 189, 146) == 0);
  assert(gate.scanout_pixel(0x15, 229, 193) == 0);
  assert(gate.scanout_pixel(0x15, 188, 146) == 0x15);
  assert(gate.scanout_pixel(0x15, 230, 193) == 0x15);
  assert(gate.scanout_pixel(0x15, 200, 194) == 0x15);
  assert(gate.scanout_word(resident_bitplane_word) == resident_bitplane_word);
  gate.write(0x1008, 0xdef0, store);
  assert(memory_writes.size() == 4);
  assert(memory_writes.back().second == 0xdef0);
  assert(gate.suppressed_words == 2);

  // Every exposed region maps to a unique, bounded rectangle. Cycling returns
  // to the default after the final region and parser accepts no aliases.
  auto region = amiga::ScanoutRegion::None;
  for (std::size_t i = 0; i < amiga::scanout_regions.size(); ++i) {
    const auto info = amiga::scanout_region_info(region);
    const auto parsed = amiga::parse_scanout_region(info.name);
    assert(parsed.has_value() && *parsed == region);
    assert(amiga::scanout_region_contains(region, info.x0, info.y0) ==
           (region != amiga::ScanoutRegion::None));
    assert(!amiga::scanout_region_contains(region, info.x1, info.y1));
    region = amiga::next_scanout_region(region);
  }
  assert(region == amiga::ScanoutRegion::None);
  assert(!amiga::parse_scanout_region("outside").has_value());
  assert(!amiga::parse_scanout_region("").has_value());
}
