// The raster dependency audit must see one whole access for each RAM
// load/store size, including 24-bit address wrapping, without altering data.
#include "m68k/cpu.hpp"

#include <cstdint>
#include <vector>

struct TestBus : m68k::Bus {
  uint8_t io_read8(uint32_t) override { return 0x12; }
  uint16_t io_read16(uint32_t) override { return 0x1234; }
  void io_write8(uint32_t, uint8_t) override {}
  void io_write16(uint32_t, uint16_t) override {}
};

struct Access { uint32_t addr; uint8_t size; bool write; };

int main() {
  TestBus bus;
  uint8_t memory[64] = {};
  bus.ram = memory;
  bus.ram_size = sizeof memory;
  std::vector<Access> seen;
  bus.ram_access_context = &seen;
  bus.ram_access_observer = [](void* ctx, uint32_t a, uint8_t s, bool w) {
    static_cast<std::vector<Access>*>(ctx)->push_back({a, s, w});
  };
  bus.write8(0x1000001, 0xab);
  bus.write16(2, 0x9876);
  bus.write32(4, 0x01234567);
  if (bus.read8(1) != 0xab || bus.read16(2) != 0x9876 ||
      bus.read32(4) != 0x01234567) return 1;
  // IO and out-of-bounds memory must never be reported as RAM.
  if (bus.read16(63) != 0x1234) return 2;
  bus.write32(63, 0xaabbccdd);
  const std::vector<Access> expected = {
      {1, 1, true}, {2, 2, true}, {4, 4, true},
      {1, 1, false}, {2, 2, false}, {4, 4, false}};
  if (seen.size() != expected.size()) return 3;
  for (size_t i = 0; i < expected.size(); ++i)
    if (seen[i].addr != expected[i].addr || seen[i].size != expected[i].size ||
        seen[i].write != expected[i].write) return 4;
  return 0;
}
