// Support code for the generated recompiled functions. Memory access
// helpers charge cycles exactly like rd<S>/wr<S> in src/m68k/cpu.cpp.
#pragma once

#include "m68k/ops.hpp"
#include "recomp/recomp.hpp"

#include <utility>

namespace recomp {

inline uint32_t rd8(m68k::Cpu& c, uint32_t a) { c.cycles += 4; return c.bus->read8(a); }
inline uint32_t rd16(m68k::Cpu& c, uint32_t a) { c.cycles += 4; return c.bus->read16(a); }
inline uint32_t rd32(m68k::Cpu& c, uint32_t a) { c.cycles += 8; return c.bus->read32(a); }
inline void wr8(m68k::Cpu& c, uint32_t a, uint32_t v) { c.cycles += 4; c.bus->write8(a, uint8_t(v)); }
inline void wr16(m68k::Cpu& c, uint32_t a, uint32_t v) { c.cycles += 4; c.bus->write16(a, uint16_t(v)); }
inline void wr32(m68k::Cpu& c, uint32_t a, uint32_t v) { c.cycles += 8; c.bus->write32(a, v); }

}  // namespace recomp
