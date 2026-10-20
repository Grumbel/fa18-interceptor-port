// Offline register file and memory for the mesh viewer's model decoder.
//
// Provides the same unqualified helper names that the polygon-op bodies in
// ops_body.inc use on the live machine (cpu(), mach(), rd*/wr*, call_c and
// the asmr register helpers of asmregs.hpp), so mesh_ops.cpp can compile
// those bodies unchanged against a private copy of the relocated executable.
// Calls into the rasteriser and other game routines go to Env::on_call.
#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <vector>

namespace mesh_viewer::emu {

struct Cpu {
  uint32_t d[8] = {}, a[8] = {};
  bool fn = false, fz = false, fv = false, fc = false;
  uint32_t op_pc = 0;
  void push32(uint32_t v);
  uint32_t pop32();
  void push16(uint16_t v);
  uint16_t pop16();
};

struct Log {
  void log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
  }
};

// Thrown instead of abort() and on accesses outside the private memory.
struct DecodeError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct Env {
  Cpu cpu;
  Log log;
  std::vector<uint8_t> mem;                    // addresses 0 .. mem.size()-1
  std::function<void(uint32_t addr)> on_call;  // call_c() target
};

Env& env();                // the decoder's current environment
void set_env(Env* e);

inline Cpu& cpu() { return env().cpu; }
inline Log& mach() { return env().log; }

inline uint8_t* at(uint32_t a, uint32_t n) {
  auto& m = env().mem;
  if (a > m.size() || m.size() - a < n) throw DecodeError("access outside model memory");
  return &m[a];
}
inline uint8_t rd8(uint32_t a) { return *at(a, 1); }
inline uint16_t rd16(uint32_t a) {
  const uint8_t* p = at(a, 2);
  return uint16_t(p[0] << 8 | p[1]);
}
inline uint32_t rd32(uint32_t a) { return uint32_t(rd16(a)) << 16 | rd16(a + 2); }
inline void wr8(uint32_t a, uint32_t v) { *at(a, 1) = uint8_t(v); }
inline void wr16(uint32_t a, uint32_t v) {
  uint8_t* p = at(a, 2);
  p[0] = uint8_t(v >> 8);
  p[1] = uint8_t(v);
}
inline void wr32(uint32_t a, uint32_t v) {
  wr16(a, v >> 16);
  wr16(a + 2, v);
}

inline void Cpu::push32(uint32_t v) {
  a[7] -= 4;
  wr32(a[7], v);
}
inline uint32_t Cpu::pop32() {
  uint32_t v = rd32(a[7]);
  a[7] += 4;
  return v;
}
inline void Cpu::push16(uint16_t v) {
  a[7] -= 2;
  wr16(a[7], v);
}
inline uint16_t Cpu::pop16() {
  uint16_t v = rd16(a[7]);
  a[7] += 2;
  return v;
}

inline uint32_t call_c(uint32_t addr, std::initializer_list<uint32_t> = {}) {
  if (env().on_call) env().on_call(addr);
  return cpu().d[0];
}

// Same semantics as native/asmregs.hpp, on this register file.
namespace asmr {
inline uint32_t& D(int n) { return cpu().d[n]; }
inline uint16_t lo(int n) { return uint16_t(cpu().d[n]); }
inline void setw(int n, uint32_t v) { cpu().d[n] = (cpu().d[n] & 0xFFFF0000u) | (v & 0xFFFF); }
inline void setb(int n, uint32_t v) { cpu().d[n] = (cpu().d[n] & 0xFFFFFF00u) | (v & 0xFF); }
inline int32_t ext(uint16_t v) { return int16_t(v); }
inline void flags(uint32_t v, int bits) {
  Cpu& c = cpu();
  uint32_t mask = bits == 32 ? 0xFFFFFFFFu : (1u << bits) - 1;
  c.fn = (v >> (bits - 1)) & 1;
  c.fz = (v & mask) == 0;
  c.fv = false;
  c.fc = false;
}
inline int32_t asr32(int32_t v, uint32_t n) {
  n &= 63;
  return n > 31 ? (v < 0 ? -1 : 0) : v >> n;
}
inline int32_t asl32(int32_t v, uint32_t n) {
  n &= 63;
  return n > 31 ? 0 : int32_t(uint32_t(v) << n);
}
inline void divuw(uint32_t& dn, uint16_t divisor) {
  if (divisor == 0) throw DecodeError("division by zero");
  uint32_t q = dn / divisor;
  if (q > 0xFFFF) return;
  dn = ((dn % divisor) << 16) | q;
}
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
inline void extw(int n) { setw(n, uint16_t(int16_t(int8_t(uint8_t(cpu().d[n]))))); }
inline void extl(int n) { cpu().d[n] = uint32_t(int32_t(int16_t(uint16_t(cpu().d[n])))); }
inline void swapw(int n) { cpu().d[n] = (cpu().d[n] << 16) | (cpu().d[n] >> 16); }
inline uint8_t lob(int n) { return uint8_t(cpu().d[n]); }
}  // namespace asmr

// Polygon-op handler at a jump-table address ($33478), or nullptr.
using OpFn = void (*)();
OpFn op_at(uint32_t addr);

}  // namespace mesh_viewer::emu
