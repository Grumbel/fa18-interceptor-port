// Helpers for register-convention (NATIVE_ASM) translations: the routine
// works directly on the live 68000 register file so that its leftover
// registers are exactly those of the original.
#pragma once

#include "native/orig.hpp"

namespace native::asmr {

inline uint32_t& D(int n) { return cpu().d[n]; }
inline uint16_t lo(int n) { return uint16_t(cpu().d[n]); }
inline void setw(int n, uint32_t v) { cpu().d[n] = (cpu().d[n] & 0xFFFF0000u) | (v & 0xFFFF); }
inline void setb(int n, uint32_t v) { cpu().d[n] = (cpu().d[n] & 0xFFFFFF00u) | (v & 0xFF); }
inline int32_t ext(uint16_t v) { return int16_t(v); }

// N and Z from the last data move; V and C cleared (move/ext/tst/clr).
inline void flags(uint32_t v, int bits) {
  m68k::Cpu& c = cpu();
  uint32_t mask = bits == 32 ? 0xFFFFFFFFu : (1u << bits) - 1;
  c.fn = (v >> (bits - 1)) & 1;
  c.fz = (v & mask) == 0;
  c.fv = false;
  c.fc = false;
}

// 68000 shifts by a register count use count mod 64.
inline int32_t asr32(int32_t v, uint32_t n) {
  n &= 63;
  return n > 31 ? (v < 0 ? -1 : 0) : v >> n;
}
inline int32_t asl32(int32_t v, uint32_t n) {
  n &= 63;
  return n > 31 ? 0 : int32_t(uint32_t(v) << n);
}

// divu.w: overflow (quotient > $ffff) leaves the destination unchanged.
inline void divuw(uint32_t& dn, uint16_t divisor) {
  if (divisor == 0) {
    mach().log("native math: division by zero at %06x\n", cpu().op_pc);
    abort();
  }
  uint32_t q = dn / divisor;
  if (q > 0xFFFF) return;
  dn = ((dn % divisor) << 16) | q;
}


// Word shifts by a register count (count mod 64).
inline uint16_t asl16(uint16_t v, uint32_t n) {
  n &= 63;
  return n > 15 ? 0 : uint16_t(v << n);
}
inline uint16_t asr16(uint16_t v, uint32_t n) {
  n &= 63;
  int16_t s = int16_t(v);
  return uint16_t(n > 15 ? (s < 0 ? -1 : 0) : s >> n);
}
inline uint32_t& A(int n) { return cpu().a[n]; }

}  // namespace native::asmr
