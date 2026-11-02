#include "amiga/regs_probe.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
  using amiga::parse_regs_probe;
  auto p = parse_regs_probe("44686,20,5a9de:w,5a9e0:w,5ac4c:w,5ac4e:w", 0x800000);
  assert(p && p->pc == 0x44686 && p->budget == 20 && p->fields.size() == 4);
  assert(p->fields[0].address == 0x5a9de && p->fields[3].address == 0x5ac4e);
  auto q = parse_regs_probe("0x44686,5,$5ac4f:b,7ffffc:l", 0x800000);
  assert(q && q->budget == 5 && q->fields[0].width == 1 && q->fields[1].width == 4);
  auto old = parse_regs_probe("44686", 0x800000);
  assert(old && old->budget == 20 && old->fields.empty());
  assert(parse_regs_probe("0X44686,0", 0x800000)->budget == 0);
  assert(!parse_regs_probe("44687", 0x800000));      // odd instruction address
  assert(!parse_regs_probe("44686,8,7ffffe:l", 0x800000));  // out of RAM
  assert(!parse_regs_probe("44686,8,5ac4e:q", 0x800000));  // invalid width
  assert(!parse_regs_probe("44686,8,5ac4e:w,", 0x800000)); // trailing field
  assert(!parse_regs_probe("44686,bad", 0x800000));
  assert(!parse_regs_probe("44686,100001", 0x800000));

  std::vector<uint8_t> ram(16);
  ram[2] = 0x12; ram[3] = 0x34; ram[4] = 0x56; ram[5] = 0x78;
  assert(amiga::probe_value(ram.data(), {2, 1}) == 0x12);
  assert(amiga::probe_value(ram.data(), {2, 2}) == 0x1234);
  assert(amiga::probe_value(ram.data(), {2, 4}) == 0x12345678);
}
