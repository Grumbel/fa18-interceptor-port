// 68000 CPU core: register state, bus interface and interpreter.
//
// The instruction semantics that are shared with the static recompiler
// (flag computation for arithmetic/logic/shift ops) live in ops.hpp.
#pragma once

#include <cstdint>
#include <functional>

namespace m68k {

class Bus {
public:
  virtual ~Bus() = default;
  // Accesses outside of RAM (custom chips, CIAs, unmapped space).
  virtual uint8_t io_read8(uint32_t addr) = 0;
  virtual uint16_t io_read16(uint32_t addr) = 0;
  virtual void io_write8(uint32_t addr, uint8_t v) = 0;
  virtual void io_write16(uint32_t addr, uint16_t v) = 0;
  // Called after every write into RAM below `watch_limit` (used to keep
  // recompiled code coherent with self-modifying code).
  virtual void ram_written(uint32_t /*addr*/) {}

  uint8_t* ram = nullptr;
  uint32_t ram_size = 0;

  uint8_t read8(uint32_t a) {
    a &= 0xFFFFFF;
    if (a < ram_size) return ram[a];
    return io_read8(a);
  }
  uint16_t read16(uint32_t a) {
    a &= 0xFFFFFF;
    if (a + 1 < ram_size) return uint16_t(ram[a] << 8 | ram[a + 1]);
    return io_read16(a);
  }
  uint32_t read32(uint32_t a) {
    a &= 0xFFFFFF;
    if (a + 3 < ram_size)
      return uint32_t(ram[a]) << 24 | uint32_t(ram[a + 1]) << 16 |
             uint32_t(ram[a + 2]) << 8 | ram[a + 3];
    return uint32_t(read16(a)) << 16 | read16(a + 2);
  }
  void write8(uint32_t a, uint8_t v) {
    a &= 0xFFFFFF;
    if (a < ram_size) { ram[a] = v; return; }
    io_write8(a, v);
  }
  void write16(uint32_t a, uint16_t v) {
    a &= 0xFFFFFF;
    if (a + 1 < ram_size) { ram[a] = uint8_t(v >> 8); ram[a + 1] = uint8_t(v); return; }
    io_write16(a, v);
  }
  void write32(uint32_t a, uint32_t v) {
    a &= 0xFFFFFF;
    if (a + 3 < ram_size) {
      ram[a] = uint8_t(v >> 24); ram[a + 1] = uint8_t(v >> 16);
      ram[a + 2] = uint8_t(v >> 8); ram[a + 3] = uint8_t(v);
      return;
    }
    write16(a, uint16_t(v >> 16));
    write16(a + 2, uint16_t(v));
  }
};

struct Cpu {
  uint32_t d[8] = {};
  uint32_t a[8] = {};   // a[7] is the active stack pointer
  uint32_t osp = 0;     // the inactive stack pointer (USP or SSP)
  uint32_t pc = 0;
  bool s = true;        // supervisor
  bool t = false;       // trace
  uint8_t imask = 7;
  bool fx = false, fn = false, fz = false, fv = false, fc = false;
  bool stopped = false;

  int64_t cycles = 0;   // approximate 68000 clock cycles executed
  uint32_t op_pc = 0;   // address of the instruction being executed

  Bus* bus = nullptr;

  // Line-A ($Axxx) and line-F handler. Return true if handled; the opcode
  // has already been consumed (pc points past it).
  std::function<bool(Cpu&, uint16_t)> line_a;
  // Called for unhandled exceptions (illegal instruction, address error…).
  std::function<void(Cpu&, int vector)> fatal;

  uint16_t sr() const {
    return uint16_t((t ? 0x8000 : 0) | (s ? 0x2000 : 0) | (imask << 8) | ccr());
  }
  uint8_t ccr() const {
    return uint8_t((fx << 4) | (fn << 3) | (fz << 2) | (fv << 1) | int(fc));
  }
  void set_ccr(uint16_t v) {
    fx = v & 0x10; fn = v & 8; fz = v & 4; fv = v & 2; fc = v & 1;
  }
  void set_sr(uint16_t v) {
    set_ccr(v);
    t = v & 0x8000;
    imask = (v >> 8) & 7;
    bool ns = v & 0x2000;
    if (ns != s) { uint32_t tmp = a[7]; a[7] = osp; osp = tmp; s = ns; }
  }

  bool cond(int cc) const;

  // Execute one instruction.
  void step();
  // Execute until `cycles` reaches `until` or the CPU stops.
  void run(int64_t until);

  // Raise an exception through the vector table in memory.
  void exception(int vector);
  // Request an interrupt at `level` (autovectored). Taken if level > imask.
  bool interrupt(int level);

  void push16(uint16_t v) { a[7] -= 2; bus->write16(a[7], v); }
  void push32(uint32_t v) { a[7] -= 4; bus->write32(a[7], v); }
  uint16_t pop16() { uint16_t v = bus->read16(a[7]); a[7] += 2; return v; }
  uint32_t pop32() { uint32_t v = bus->read32(a[7]); a[7] += 4; return v; }

  // Plain register state (for cheap save/restore by host code).
  struct Regs {
    uint32_t d[8], a[8], osp, pc;
    bool s, t, fx, fn, fz, fv, fc, stopped;
    uint8_t imask;
  };
  Regs save() const {
    Regs r;
    for (int i = 0; i < 8; ++i) { r.d[i] = d[i]; r.a[i] = a[i]; }
    r.osp = osp; r.pc = pc; r.s = s; r.t = t; r.imask = imask;
    r.fx = fx; r.fn = fn; r.fz = fz; r.fv = fv; r.fc = fc; r.stopped = stopped;
    return r;
  }
  void restore(const Regs& r) {
    for (int i = 0; i < 8; ++i) { d[i] = r.d[i]; a[i] = r.a[i]; }
    osp = r.osp; pc = r.pc; s = r.s; t = r.t; imask = r.imask;
    fx = r.fx; fn = r.fn; fz = r.fz; fv = r.fv; fc = r.fc; stopped = r.stopped;
  }

  uint16_t fetch16() { uint16_t v = bus->read16(pc); pc += 2; cycles += 4; return v; }
  uint32_t fetch32() { uint32_t v = bus->read32(pc); pc += 4; cycles += 8; return v; }
};

}  // namespace m68k
