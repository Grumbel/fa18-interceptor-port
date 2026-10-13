// 68000 ALU semantics shared by the interpreter and recompiled code.
// All helpers take operands already truncated to the operation size and
// return the truncated result, updating the condition codes in `c`.
#pragma once

#include "cpu.hpp"

namespace m68k {

template <int S> struct Size;
template <> struct Size<1> { static constexpr uint32_t mask = 0xFF, msb = 0x80; static constexpr int bits = 8; };
template <> struct Size<2> { static constexpr uint32_t mask = 0xFFFF, msb = 0x8000; static constexpr int bits = 16; };
template <> struct Size<4> { static constexpr uint32_t mask = 0xFFFFFFFF, msb = 0x80000000; static constexpr int bits = 32; };

template <int S> inline int32_t sext(uint32_t v) {
  if constexpr (S == 1) return int8_t(v);
  else if constexpr (S == 2) return int16_t(v);
  else return int32_t(v);
}

template <int S> inline void set_nz(Cpu& c, uint32_t r) {
  c.fn = r & Size<S>::msb;
  c.fz = (r & Size<S>::mask) == 0;
}

// MOVE, AND, OR, EOR, NOT, CLR, TST, EXT, SWAP: N,Z set; V,C cleared.
template <int S> inline uint32_t logic(Cpu& c, uint32_t r) {
  r &= Size<S>::mask;
  set_nz<S>(c, r);
  c.fv = c.fc = false;
  return r;
}

template <int S> inline uint32_t add(Cpu& c, uint32_t s, uint32_t d) {
  constexpr uint32_t M = Size<S>::mask, H = Size<S>::msb;
  uint32_t r = (d + s) & M;
  set_nz<S>(c, r);
  c.fv = ((s ^ r) & (d ^ r)) & H;
  c.fc = c.fx = ((s & d) | (~r & (s | d))) & H;
  return r;
}

template <int S> inline uint32_t addx(Cpu& c, uint32_t s, uint32_t d) {
  constexpr uint32_t M = Size<S>::mask, H = Size<S>::msb;
  uint32_t r = (d + s + (c.fx ? 1 : 0)) & M;
  c.fn = r & H;
  if (r) c.fz = false;
  c.fv = ((s ^ r) & (d ^ r)) & H;
  c.fc = c.fx = ((s & d) | (~r & (s | d))) & H;
  return r;
}

template <int S> inline uint32_t sub(Cpu& c, uint32_t s, uint32_t d) {
  constexpr uint32_t M = Size<S>::mask, H = Size<S>::msb;
  uint32_t r = (d - s) & M;
  set_nz<S>(c, r);
  c.fv = ((s ^ d) & (r ^ d)) & H;
  c.fc = c.fx = ((s & ~d) | (r & ~d) | (s & r)) & H;
  return r;
}

template <int S> inline uint32_t subx(Cpu& c, uint32_t s, uint32_t d) {
  constexpr uint32_t M = Size<S>::mask, H = Size<S>::msb;
  uint32_t r = (d - s - (c.fx ? 1 : 0)) & M;
  c.fn = r & H;
  if (r) c.fz = false;
  c.fv = ((s ^ d) & (r ^ d)) & H;
  c.fc = c.fx = ((s & ~d) | (r & ~d) | (s & r)) & H;
  return r;
}

// CMP: like SUB but X unaffected and no result.
template <int S> inline void cmp(Cpu& c, uint32_t s, uint32_t d) {
  constexpr uint32_t M = Size<S>::mask, H = Size<S>::msb;
  uint32_t r = (d - s) & M;
  set_nz<S>(c, r);
  c.fv = ((s ^ d) & (r ^ d)) & H;
  c.fc = ((s & ~d) | (r & ~d) | (s & r)) & H;
}

template <int S> inline uint32_t neg(Cpu& c, uint32_t d) { return sub<S>(c, d, 0); }
template <int S> inline uint32_t negx(Cpu& c, uint32_t d) { return subx<S>(c, d, 0); }

inline uint32_t mulu(Cpu& c, uint32_t s, uint32_t d) {
  uint32_t r = (s & 0xFFFF) * (d & 0xFFFF);
  set_nz<4>(c, r);
  c.fv = c.fc = false;
  return r;
}

inline uint32_t muls(Cpu& c, uint32_t s, uint32_t d) {
  uint32_t r = uint32_t(int32_t(int16_t(s)) * int32_t(int16_t(d)));
  set_nz<4>(c, r);
  c.fv = c.fc = false;
  return r;
}

// DIVU/DIVS: returns false on divide by zero (caller raises the trap).
// On overflow the destination is unchanged and V is set.
inline bool divu(Cpu& c, uint32_t s, uint32_t& d) {
  s &= 0xFFFF;
  if (s == 0) { c.fc = false; return false; }
  uint32_t q = d / s, r = d % s;
  c.fc = false;
  if (q > 0xFFFF) { c.fv = true; c.fn = true; c.fz = false; return true; }
  d = (r << 16) | q;
  c.fv = false;
  c.fn = q & 0x8000;
  c.fz = q == 0;
  return true;
}

inline bool divs(Cpu& c, uint32_t s, uint32_t& d) {
  int32_t sv = int16_t(s);
  if (sv == 0) { c.fc = false; return false; }
  int32_t dv = int32_t(d);
  c.fc = false;
  if (dv == INT32_MIN && sv == -1) { c.fv = true; c.fn = true; c.fz = false; return true; }
  int32_t q = dv / sv, r = dv % sv;
  if (q < -32768 || q > 32767) { c.fv = true; c.fn = true; c.fz = false; return true; }
  d = (uint32_t(r) << 16) | (uint32_t(q) & 0xFFFF);
  c.fv = false;
  c.fn = q < 0;
  c.fz = q == 0;
  return true;
}

// BCD per Flamewing's analysis of real 68000 silicon (incl. undefined V).
inline uint32_t abcd(Cpu& c, uint32_t s, uint32_t d) {
  s &= 0xFF; d &= 0xFF;
  uint32_t ss = s + d + (c.fx ? 1 : 0);
  uint32_t bc = ((s & d) | (~ss & (s | d))) & 0x88;
  uint32_t dc = (((ss + 0x66) ^ ss) & 0x110) >> 1;
  uint32_t corf = (bc | dc) - ((bc | dc) >> 2);
  uint32_t r = ss + corf;
  c.fc = c.fx = ((bc | (ss & ~r)) >> 7) & 1;
  c.fv = ((~ss & r) >> 7) & 1;
  r &= 0xFF;
  c.fn = r & 0x80;
  if (r) c.fz = false;
  return r;
}

inline uint32_t sbcd(Cpu& c, uint32_t s, uint32_t d) {
  s &= 0xFF; d &= 0xFF;
  uint32_t dd = (d - s - (c.fx ? 1 : 0)) & 0xFFFF;
  uint32_t bc = ((~d & s) | (dd & ~d) | (dd & s)) & 0x88;
  uint32_t corf = bc - (bc >> 2);
  uint32_t r = (dd - corf) & 0xFFFF;
  c.fc = c.fx = ((bc | (~dd & r)) >> 7) & 1;
  c.fv = ((dd & ~r) >> 7) & 1;
  r &= 0xFF;
  c.fn = r & 0x80;
  if (r) c.fz = false;
  return r;
}

// Shifts/rotates: `n` is the shift count (0..63). Return result.
template <int S> inline uint32_t asl(Cpu& c, uint32_t d, unsigned n) {
  constexpr uint32_t M = Size<S>::mask, H = Size<S>::msb;
  constexpr int B = Size<S>::bits;
  d &= M;
  if (n == 0) { set_nz<S>(c, d); c.fv = c.fc = false; return d; }
  // V set if the MSB changes at any time during the shift.
  bool v = false;
  uint32_t r = d;
  bool carry = false;
  for (unsigned i = 0; i < n; ++i) {
    carry = r & H;
    r = (r << 1) & M;
    if (bool(r & H) != carry) v = true;
  }
  (void)B;
  c.fc = c.fx = carry;
  c.fv = v;
  set_nz<S>(c, r);
  return r;
}

template <int S> inline uint32_t asr(Cpu& c, uint32_t d, unsigned n) {
  constexpr uint32_t M = Size<S>::mask;
  constexpr int B = Size<S>::bits;
  d &= M;
  if (n == 0) { set_nz<S>(c, d); c.fv = c.fc = false; return d; }
  int32_t sv = sext<S>(d);
  uint32_t r;
  if (n >= unsigned(B)) {
    r = sv < 0 ? M : 0;
    c.fc = c.fx = sv < 0;
  } else {
    c.fc = c.fx = (sv >> (n - 1)) & 1;
    r = uint32_t(sv >> n) & M;
  }
  c.fv = false;
  set_nz<S>(c, r);
  return r;
}

template <int S> inline uint32_t lsl(Cpu& c, uint32_t d, unsigned n) {
  constexpr uint32_t M = Size<S>::mask;
  constexpr int B = Size<S>::bits;
  d &= M;
  if (n == 0) { set_nz<S>(c, d); c.fv = c.fc = false; return d; }
  uint32_t r;
  if (n > unsigned(B)) { r = 0; c.fc = c.fx = false; }
  else {
    c.fc = c.fx = (uint64_t(d) >> (B - n)) & 1;
    r = uint32_t(uint64_t(d) << n) & M;
  }
  c.fv = false;
  set_nz<S>(c, r);
  return r;
}

template <int S> inline uint32_t lsr(Cpu& c, uint32_t d, unsigned n) {
  constexpr uint32_t M = Size<S>::mask;
  constexpr int B = Size<S>::bits;
  d &= M;
  if (n == 0) { set_nz<S>(c, d); c.fv = c.fc = false; return d; }
  uint32_t r;
  if (n > unsigned(B)) { r = 0; c.fc = c.fx = false; }
  else {
    c.fc = c.fx = (uint64_t(d) >> (n - 1)) & 1;
    r = uint32_t(uint64_t(d) >> n) & M;
  }
  c.fv = false;
  set_nz<S>(c, r);
  return r;
}

template <int S> inline uint32_t rol(Cpu& c, uint32_t d, unsigned n) {
  constexpr uint32_t M = Size<S>::mask;
  constexpr int B = Size<S>::bits;
  d &= M;
  c.fv = false;
  if (n == 0) { c.fc = false; set_nz<S>(c, d); return d; }
  unsigned k = n % B;
  uint32_t r = k ? ((d << k) | (d >> (B - k))) & M : d;
  c.fc = r & 1;
  set_nz<S>(c, r);
  return r;
}

template <int S> inline uint32_t ror(Cpu& c, uint32_t d, unsigned n) {
  constexpr uint32_t M = Size<S>::mask, H = Size<S>::msb;
  constexpr int B = Size<S>::bits;
  d &= M;
  c.fv = false;
  if (n == 0) { c.fc = false; set_nz<S>(c, d); return d; }
  unsigned k = n % B;
  uint32_t r = k ? ((d >> k) | (d << (B - k))) & M : d;
  c.fc = r & H;
  set_nz<S>(c, r);
  return r;
}

template <int S> inline uint32_t roxl(Cpu& c, uint32_t d, unsigned n) {
  constexpr uint32_t M = Size<S>::mask, H = Size<S>::msb;
  d &= M;
  bool x = c.fx;
  for (unsigned i = 0; i < n; ++i) {
    bool out = d & H;
    d = ((d << 1) | (x ? 1 : 0)) & M;
    x = out;
  }
  c.fx = x;
  c.fc = x;
  c.fv = false;
  set_nz<S>(c, d);
  return d;
}

template <int S> inline uint32_t roxr(Cpu& c, uint32_t d, unsigned n) {
  constexpr uint32_t M = Size<S>::mask, H = Size<S>::msb;
  d &= M;
  bool x = c.fx;
  for (unsigned i = 0; i < n; ++i) {
    bool out = d & 1;
    d = (d >> 1) | (x ? H : 0);
    x = out;
  }
  c.fx = x;
  c.fc = x;
  c.fv = false;
  set_nz<S>(c, d);
  return d;
}

inline bool Cpu::cond(int cc) const {
  switch (cc & 15) {
    case 0: return true;
    case 1: return false;
    case 2: return !fc && !fz;          // HI
    case 3: return fc || fz;            // LS
    case 4: return !fc;                 // CC
    case 5: return fc;                  // CS
    case 6: return !fz;                 // NE
    case 7: return fz;                  // EQ
    case 8: return !fv;                 // VC
    case 9: return fv;                  // VS
    case 10: return !fn;                // PL
    case 11: return fn;                 // MI
    case 12: return fn == fv;           // GE
    case 13: return fn != fv;           // LT
    case 14: return !fz && fn == fv;    // GT
    default: return fz || fn != fv;     // LE
  }
}

}  // namespace m68k
