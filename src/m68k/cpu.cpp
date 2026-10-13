// 68000 interpreter.
#include "cpu.hpp"
#include "ops.hpp"

#include <cstdio>
#include <type_traits>

namespace m68k {

namespace {

struct Ea {
  uint8_t mode;   // 0 Dn, 1 An, 2 memory, 3 immediate
  uint8_t reg;
  uint32_t addr;  // memory address or immediate value
};

enum { EA_D = 0, EA_A = 1, EA_M = 2, EA_I = 3 };

uint32_t index_ext(Cpu& c, uint32_t base) {
  uint16_t ext = c.fetch16();
  int r = (ext >> 12) & 7;
  uint32_t x = (ext & 0x8000) ? c.a[r] : c.d[r];
  if (!(ext & 0x800)) x = uint32_t(int32_t(int16_t(x)));
  c.cycles += 2;
  return base + uint32_t(int32_t(int8_t(ext))) + x;
}

// Resolve an effective address (applying postinc/predec side effects).
template <int S>
Ea resolve(Cpu& c, int mode, int reg) {
  switch (mode) {
    case 0: return {EA_D, uint8_t(reg), 0};
    case 1: return {EA_A, uint8_t(reg), 0};
    case 2: return {EA_M, 0, c.a[reg]};
    case 3: {
      uint32_t a = c.a[reg];
      c.a[reg] += (S == 1 && reg == 7) ? 2 : S;
      return {EA_M, 0, a};
    }
    case 4: {
      c.a[reg] -= (S == 1 && reg == 7) ? 2 : S;
      c.cycles += 2;
      return {EA_M, 0, c.a[reg]};
    }
    case 5: {
      uint32_t a = c.a[reg] + uint32_t(int32_t(int16_t(c.fetch16())));
      return {EA_M, 0, a};
    }
    case 6: return {EA_M, 0, index_ext(c, c.a[reg])};
    case 7:
      switch (reg) {
        case 0: return {EA_M, 0, uint32_t(int32_t(int16_t(c.fetch16())))};
        case 1: return {EA_M, 0, c.fetch32()};
        case 2: {
          uint32_t base = c.pc;
          return {EA_M, 0, base + uint32_t(int32_t(int16_t(c.fetch16())))};
        }
        case 3: {
          uint32_t base = c.pc;
          return {EA_M, 0, index_ext(c, base)};
        }
        case 4:
          if (S == 4) return {EA_I, 0, c.fetch32()};
          if (S == 2) return {EA_I, 0, c.fetch16()};
          return {EA_I, 0, uint32_t(c.fetch16() & 0xFF)};
      }
  }
  return {EA_I, 0, 0};
}

template <int S>
uint32_t rd(Cpu& c, const Ea& e) {
  switch (e.mode) {
    case EA_D: return c.d[e.reg] & Size<S>::mask;
    case EA_A: return c.a[e.reg] & Size<S>::mask;
    case EA_I: return e.addr;
    default:
      if (S == 1) { c.cycles += 4; return c.bus->read8(e.addr); }
      if (S == 2) { c.cycles += 4; return c.bus->read16(e.addr); }
      c.cycles += 8;
      return c.bus->read32(e.addr);
  }
}

template <int S>
void wr(Cpu& c, const Ea& e, uint32_t v) {
  switch (e.mode) {
    case EA_D:
      c.d[e.reg] = (c.d[e.reg] & ~Size<S>::mask) | (v & Size<S>::mask);
      return;
    case EA_A:
      c.a[e.reg] = S == 2 ? uint32_t(int32_t(int16_t(v))) : v;
      return;
    case EA_I: return;
    default:
      if (S == 1) { c.cycles += 4; c.bus->write8(e.addr, uint8_t(v)); }
      else if (S == 2) { c.cycles += 4; c.bus->write16(e.addr, uint16_t(v)); }
      else { c.cycles += 8; c.bus->write32(e.addr, v); }
  }
}

template <int S>
uint32_t read_ea(Cpu& c, int mode, int reg) {
  Ea e = resolve<S>(c, mode, reg);
  return rd<S>(c, e);
}

// Control addressing (LEA/PEA/JMP/JSR/MOVEM): address only.
uint32_t ctrl_addr(Cpu& c, int mode, int reg) { return resolve<4>(c, mode, reg).addr; }

void illegal(Cpu& c) {
  c.pc = c.op_pc;
  c.exception(4);
}

template <int S> void set_dreg(Cpu& c, int r, uint32_t v) {
  c.d[r] = (c.d[r] & ~Size<S>::mask) | (v & Size<S>::mask);
}

// ---- group 0: immediate ops, bit ops, MOVEP ----------------------------
template <int S>
void imm_op(Cpu& c, int kind, int mode, int reg) {
  uint32_t imm;
  if (S == 4) imm = c.fetch32();
  else if (S == 2) imm = c.fetch16();
  else imm = c.fetch16() & 0xFF;
  Ea e = resolve<S>(c, mode, reg);
  uint32_t d = rd<S>(c, e);
  switch (kind) {
    case 0: wr<S>(c, e, logic<S>(c, d | imm)); break;      // ORI
    case 1: wr<S>(c, e, logic<S>(c, d & imm)); break;      // ANDI
    case 2: wr<S>(c, e, sub<S>(c, imm, d)); break;         // SUBI
    case 3: wr<S>(c, e, add<S>(c, imm, d)); break;         // ADDI
    case 5: wr<S>(c, e, logic<S>(c, d ^ imm)); break;      // EORI
    case 6: cmp<S>(c, imm, d); break;                       // CMPI
  }
}

void bit_op(Cpu& c, int kind, uint32_t bit, int mode, int reg) {
  if (mode == 0) {
    bit &= 31;
    uint32_t m = 1u << bit;
    c.fz = !(c.d[reg] & m);
    switch (kind) {
      case 1: c.d[reg] ^= m; break;
      case 2: c.d[reg] &= ~m; break;
      case 3: c.d[reg] |= m; break;
    }
    c.cycles += 2;
    return;
  }
  bit &= 7;
  Ea e = resolve<1>(c, mode, reg);
  uint32_t v = rd<1>(c, e);
  uint32_t m = 1u << bit;
  c.fz = !(v & m);
  switch (kind) {
    case 1: wr<1>(c, e, v ^ m); break;
    case 2: wr<1>(c, e, v & ~m); break;
    case 3: wr<1>(c, e, v | m); break;
  }
}

void group0(Cpu& c, uint16_t op) {
  int mode = (op >> 3) & 7, reg = op & 7;
  if (op & 0x100) {
    int dn = (op >> 9) & 7;
    if (mode == 1) {  // MOVEP
      uint32_t addr = c.a[reg] + uint32_t(int32_t(int16_t(c.fetch16())));
      int opm = (op >> 6) & 3;
      switch (opm) {
        case 0: {  // word mem->reg
          uint32_t v = (c.bus->read8(addr) << 8) | c.bus->read8(addr + 2);
          set_dreg<2>(c, dn, v);
          break;
        }
        case 1: {
          uint32_t v = (uint32_t(c.bus->read8(addr)) << 24) | (c.bus->read8(addr + 2) << 16) |
                       (c.bus->read8(addr + 4) << 8) | c.bus->read8(addr + 6);
          c.d[dn] = v;
          break;
        }
        case 2:
          c.bus->write8(addr, uint8_t(c.d[dn] >> 8));
          c.bus->write8(addr + 2, uint8_t(c.d[dn]));
          break;
        case 3:
          c.bus->write8(addr, uint8_t(c.d[dn] >> 24));
          c.bus->write8(addr + 2, uint8_t(c.d[dn] >> 16));
          c.bus->write8(addr + 4, uint8_t(c.d[dn] >> 8));
          c.bus->write8(addr + 6, uint8_t(c.d[dn]));
          break;
      }
      c.cycles += 8;
      return;
    }
    bit_op(c, (op >> 6) & 3, c.d[dn], mode, reg);
    return;
  }
  int kind = (op >> 9) & 7;
  int sz = (op >> 6) & 3;
  if (kind == 4) {  // static bit ops
    uint32_t bit = c.fetch16() & 0xFF;
    bit_op(c, sz, bit, mode, reg);
    return;
  }
  if (kind == 7 || sz == 3) { illegal(c); return; }
  if (mode == 7 && reg == 4) {  // to CCR / SR
    uint16_t imm = c.fetch16();
    if (sz == 0) {
      uint8_t ccr = c.ccr();
      switch (kind) {
        case 0: c.set_ccr(ccr | imm); break;
        case 1: c.set_ccr(ccr & imm); break;
        case 5: c.set_ccr(ccr ^ imm); break;
        default: illegal(c); break;
      }
      return;
    }
    if (sz == 1) {
      if (!c.s) { c.pc = c.op_pc; c.exception(8); return; }
      uint16_t sr = c.sr();
      switch (kind) {
        case 0: c.set_sr(sr | imm); break;
        case 1: c.set_sr(sr & imm); break;
        case 5: c.set_sr(sr ^ imm); break;
        default: illegal(c); break;
      }
      return;
    }
    illegal(c);
    return;
  }
  switch (sz) {
    case 0: imm_op<1>(c, kind, mode, reg); break;
    case 1: imm_op<2>(c, kind, mode, reg); break;
    case 2: imm_op<4>(c, kind, mode, reg); break;
  }
}

// ---- MOVE ---------------------------------------------------------------
template <int S>
void move(Cpu& c, uint16_t op) {
  int smode = (op >> 3) & 7, sreg = op & 7;
  int dmode = (op >> 6) & 7, dreg = (op >> 9) & 7;
  uint32_t v = read_ea<S>(c, smode, sreg);
  if (dmode == 1) {  // MOVEA
    c.a[dreg] = S == 2 ? uint32_t(int32_t(int16_t(v))) : v;
    return;
  }
  Ea e = resolve<S>(c, dmode, dreg);
  logic<S>(c, v);
  wr<S>(c, e, v);
}

// ---- group 4: misc --------------------------------------------------------
void movem(Cpu& c, uint16_t op) {
  bool to_reg = op & 0x400;
  bool lng = op & 0x40;
  int mode = (op >> 3) & 7, reg = op & 7;
  uint16_t mask = c.fetch16();
  int sz = lng ? 4 : 2;
  if (!to_reg && mode == 4) {
    // predecrement: mask bit 0 = A7 … bit 15 = D0
    uint32_t addr = c.a[reg];
    uint32_t orig = c.a[reg];
    for (int i = 0; i < 16; ++i) {
      if (!(mask & (1 << i))) continue;
      int r = 15 - i;
      uint32_t v = r < 8 ? c.d[r] : c.a[r - 8];
      if (r - 8 == reg) v = orig;
      addr -= sz;
      if (lng) c.bus->write32(addr, v); else c.bus->write16(addr, uint16_t(v));
      c.cycles += lng ? 8 : 4;
    }
    c.a[reg] = addr;
    return;
  }
  uint32_t addr;
  if (mode == 3) addr = c.a[reg];
  else addr = ctrl_addr(c, mode, reg);
  for (int i = 0; i < 16; ++i) {
    if (!(mask & (1 << i))) continue;
    if (to_reg) {
      uint32_t v = lng ? c.bus->read32(addr) : uint32_t(int32_t(int16_t(c.bus->read16(addr))));
      if (i < 8) c.d[i] = v; else c.a[i - 8] = v;
    } else {
      uint32_t v = i < 8 ? c.d[i] : c.a[i - 8];
      if (lng) c.bus->write32(addr, v); else c.bus->write16(addr, uint16_t(v));
    }
    addr += sz;
    c.cycles += lng ? 8 : 4;
  }
  if (to_reg) c.cycles += 4;
  if (mode == 3) c.a[reg] = addr;
}

template <int S>
void unary(Cpu& c, int kind, int mode, int reg) {
  Ea e = resolve<S>(c, mode, reg);
  switch (kind) {
    case 0: wr<S>(c, e, negx<S>(c, rd<S>(c, e))); break;  // NEGX
    case 1:                                                // CLR
      if (e.mode == EA_M) rd<S>(c, e);  // 68000 reads before clearing
      wr<S>(c, e, logic<S>(c, 0));
      break;
    case 2: wr<S>(c, e, neg<S>(c, rd<S>(c, e))); break;    // NEG
    case 3: wr<S>(c, e, logic<S>(c, ~rd<S>(c, e))); break; // NOT
    case 5: logic<S>(c, rd<S>(c, e)); break;               // TST
  }
}

void group4(Cpu& c, uint16_t op) {
  int mode = (op >> 3) & 7, reg = op & 7;
  if (op & 0x100) {
    int an = (op >> 9) & 7;
    if ((op & 0x1C0) == 0x1C0) {  // LEA
      c.a[an] = ctrl_addr(c, mode, reg);
      return;
    }
    if ((op & 0x1C0) == 0x180) {  // CHK.W
      int16_t bound = int16_t(read_ea<2>(c, mode, reg));
      int16_t v = int16_t(c.d[an]);
      c.fz = v == 0; c.fv = c.fc = false;
      if (v < 0) { c.fn = true; c.exception(6); }
      else if (v > bound) { c.fn = false; c.exception(6); }
      else c.fn = false;
      return;
    }
    illegal(c);
    return;
  }
  int sub = (op >> 8) & 0xF;
  int sz = (op >> 6) & 3;
  switch (sub) {
    case 0x0:
      if (sz == 3) {  // MOVE from SR
        Ea e = resolve<2>(c, mode, reg);
        wr<2>(c, e, c.sr());
      } else if (sz == 0) unary<1>(c, 0, mode, reg);
      else if (sz == 1) unary<2>(c, 0, mode, reg);
      else unary<4>(c, 0, mode, reg);
      return;
    case 0x2:
      if (sz == 3) { illegal(c); return; }
      if (sz == 0) unary<1>(c, 1, mode, reg);
      else if (sz == 1) unary<2>(c, 1, mode, reg);
      else unary<4>(c, 1, mode, reg);
      return;
    case 0x4:
      if (sz == 3) {  // MOVE to CCR
        c.set_ccr(read_ea<2>(c, mode, reg));
        return;
      }
      if (sz == 0) unary<1>(c, 2, mode, reg);
      else if (sz == 1) unary<2>(c, 2, mode, reg);
      else unary<4>(c, 2, mode, reg);
      return;
    case 0x6:
      if (sz == 3) {  // MOVE to SR
        if (!c.s) { c.pc = c.op_pc; c.exception(8); return; }
        c.set_sr(read_ea<2>(c, mode, reg));
        return;
      }
      if (sz == 0) unary<1>(c, 3, mode, reg);
      else if (sz == 1) unary<2>(c, 3, mode, reg);
      else unary<4>(c, 3, mode, reg);
      return;
    case 0x8:
      if (sz == 0) {  // NBCD
        Ea e = resolve<1>(c, mode, reg);
        wr<1>(c, e, sbcd(c, rd<1>(c, e), 0));
        return;
      }
      if (sz == 1) {
        if (mode == 0) {  // SWAP
          uint32_t v = c.d[reg];
          c.d[reg] = logic<4>(c, (v >> 16) | (v << 16));
        } else {  // PEA
          uint32_t a = ctrl_addr(c, mode, reg);
          c.push32(a);
        }
        return;
      }
      if (mode == 0) {  // EXT
        if (sz == 2) set_dreg<2>(c, reg, logic<2>(c, uint32_t(int32_t(int8_t(c.d[reg])))));
        else c.d[reg] = logic<4>(c, uint32_t(int32_t(int16_t(c.d[reg]))));
        return;
      }
      movem(c, op);
      return;
    case 0xA:
      if (sz == 3) {
        if (op == 0x4AFC) { illegal(c); return; }
        Ea e = resolve<1>(c, mode, reg);  // TAS
        uint32_t v = rd<1>(c, e);
        logic<1>(c, v);
        wr<1>(c, e, v | 0x80);
        return;
      }
      if (sz == 0) unary<1>(c, 5, mode, reg);
      else if (sz == 1) unary<2>(c, 5, mode, reg);
      else unary<4>(c, 5, mode, reg);
      return;
    case 0xC:
      if (sz >= 2) { movem(c, op); return; }
      illegal(c);
      return;
    case 0xE:
      if (sz == 1) {
        int k = (op >> 3) & 7;
        switch (k) {
          case 0: case 1: c.exception(32 + (op & 15)); return;  // TRAP
          case 2: {  // LINK
            int16_t disp = int16_t(c.fetch16());
            c.push32(c.a[reg]);
            c.a[reg] = c.a[7];
            c.a[7] += uint32_t(int32_t(disp));
            return;
          }
          case 3:  // UNLK
            c.a[7] = c.a[reg];
            c.a[reg] = c.pop32();
            return;
          case 4: case 5:  // MOVE USP
            if (!c.s) { c.pc = c.op_pc; c.exception(8); return; }
            if (k == 4) c.osp = c.a[reg]; else c.a[reg] = c.osp;
            return;
          case 6:
            switch (op & 7) {
              case 0:  // RESET
                if (!c.s) { c.pc = c.op_pc; c.exception(8); }
                return;
              case 1: return;  // NOP
              case 2:          // STOP
                if (!c.s) { c.pc = c.op_pc; c.exception(8); return; }
                c.set_sr(c.fetch16());
                c.stopped = true;
                return;
              case 3: {        // RTE
                if (!c.s) { c.pc = c.op_pc; c.exception(8); return; }
                uint16_t sr = c.pop16();
                c.pc = c.pop32();
                c.set_sr(sr);
                return;
              }
              case 5: c.pc = c.pop32(); return;  // RTS
              case 6: if (c.fv) c.exception(7); return;  // TRAPV
              case 7: {        // RTR
                c.set_ccr(c.pop16());
                c.pc = c.pop32();
                return;
              }
            }
            illegal(c);
            return;
        }
        illegal(c);
        return;
      }
      if (sz == 2) {  // JSR
        uint32_t a = ctrl_addr(c, mode, reg);
        c.push32(c.pc);
        c.pc = a;
        return;
      }
      if (sz == 3) {  // JMP
        c.pc = ctrl_addr(c, mode, reg);
        return;
      }
      illegal(c);
      return;
  }
  illegal(c);
}

// ---- group 5: ADDQ/SUBQ/Scc/DBcc -----------------------------------------
template <int S>
void addq_subq(Cpu& c, bool is_sub, uint32_t q, int mode, int reg) {
  if (mode == 1) {  // address register: whole register, no flags
    c.a[reg] += is_sub ? uint32_t(-int32_t(q)) : q;
    return;
  }
  Ea e = resolve<S>(c, mode, reg);
  uint32_t d = rd<S>(c, e);
  wr<S>(c, e, is_sub ? sub<S>(c, q, d) : add<S>(c, q, d));
}

void group5(Cpu& c, uint16_t op) {
  int mode = (op >> 3) & 7, reg = op & 7;
  int sz = (op >> 6) & 3;
  if (sz == 3) {
    int cc = (op >> 8) & 15;
    if (mode == 1) {  // DBcc
      int16_t disp = int16_t(c.fetch16());
      if (!c.cond(cc)) {
        uint16_t v = uint16_t(c.d[reg]) - 1;
        set_dreg<2>(c, reg, v);
        if (v != 0xFFFF) c.pc = c.pc - 2 + uint32_t(int32_t(disp));
      }
      return;
    }
    Ea e = resolve<1>(c, mode, reg);
    wr<1>(c, e, c.cond(cc) ? 0xFF : 0);
    return;
  }
  uint32_t q = (op >> 9) & 7;
  if (q == 0) q = 8;
  bool is_sub = op & 0x100;
  switch (sz) {
    case 0: addq_subq<1>(c, is_sub, q, mode, reg); break;
    case 1: addq_subq<2>(c, is_sub, q, mode, reg); break;
    case 2: addq_subq<4>(c, is_sub, q, mode, reg); break;
  }
}

// ---- group 6: branches ----------------------------------------------------
void group6(Cpu& c, uint16_t op) {
  int cc = (op >> 8) & 15;
  uint32_t base = c.pc;
  int32_t disp = int8_t(op & 0xFF);
  if (disp == 0) disp = int16_t(c.fetch16());
  if (cc == 1) {  // BSR
    c.push32(c.pc);
    c.pc = base + uint32_t(disp);
    return;
  }
  if (c.cond(cc)) {
    c.pc = base + uint32_t(disp);
    c.cycles += 2;
  }
}

// ---- groups 8/9/B/C/D: two-operand arithmetic ------------------------------
template <int S>
void arith(Cpu& c, int grp, bool to_ea, int dn, int mode, int reg) {
  if (!to_ea) {
    uint32_t s = read_ea<S>(c, mode, reg);
    uint32_t d = c.d[dn] & Size<S>::mask;
    uint32_t r = 0;
    switch (grp) {
      case 0x8: r = logic<S>(c, d | s); break;
      case 0x9: r = sub<S>(c, s, d); break;
      case 0xB: cmp<S>(c, s, d); return;
      case 0xC: r = logic<S>(c, d & s); break;
      case 0xD: r = add<S>(c, s, d); break;
    }
    set_dreg<S>(c, dn, r);
    return;
  }
  Ea e = resolve<S>(c, mode, reg);
  uint32_t d = rd<S>(c, e);
  uint32_t s = c.d[dn] & Size<S>::mask;
  uint32_t r = 0;
  switch (grp) {
    case 0x8: r = logic<S>(c, d | s); break;
    case 0x9: r = sub<S>(c, s, d); break;
    case 0xB: r = logic<S>(c, d ^ s); break;  // EOR
    case 0xC: r = logic<S>(c, d & s); break;
    case 0xD: r = add<S>(c, s, d); break;
  }
  wr<S>(c, e, r);
}

// ADDX/SUBX/ABCD/SBCD/CMPM-like register or -(An) pairs.
template <int S>
void xop(Cpu& c, int grp, bool mem, int rx, int ry) {
  // rx = destination (bits 9-11), ry = source (bits 0-2)
  if (!mem) {
    uint32_t s = c.d[ry] & Size<S>::mask, d = c.d[rx] & Size<S>::mask;
    uint32_t r;
    if (grp == 0xD) r = addx<S>(c, s, d);
    else if (grp == 0x9) r = subx<S>(c, s, d);
    else if (grp == 0xC) r = abcd(c, s, d);
    else r = sbcd(c, s, d);
    set_dreg<S>(c, rx, r);
    return;
  }
  Ea es = resolve<S>(c, 4, ry);
  uint32_t s = rd<S>(c, es);
  Ea ed = resolve<S>(c, 4, rx);
  uint32_t d = rd<S>(c, ed);
  uint32_t r;
  if (grp == 0xD) r = addx<S>(c, s, d);
  else if (grp == 0x9) r = subx<S>(c, s, d);
  else if (grp == 0xC) r = abcd(c, s, d);
  else r = sbcd(c, s, d);
  wr<S>(c, ed, r);
}

void group_arith(Cpu& c, uint16_t op) {
  int grp = op >> 12;
  int dn = (op >> 9) & 7;
  int opm = (op >> 6) & 7;
  int mode = (op >> 3) & 7, reg = op & 7;

  if (opm == 3 || opm == 7) {
    switch (grp) {
      case 0x8: {  // DIVU / DIVS
        uint32_t s = read_ea<2>(c, mode, reg);
        bool ok = opm == 3 ? divu(c, s, c.d[dn]) : divs(c, s, c.d[dn]);
        c.cycles += 136;
        if (!ok) c.exception(5);
        return;
      }
      case 0xC: {  // MULU / MULS
        uint32_t s = read_ea<2>(c, mode, reg);
        c.d[dn] = opm == 3 ? mulu(c, s, c.d[dn]) : muls(c, s, c.d[dn]);
        c.cycles += 50;
        return;
      }
      case 0x9: case 0xB: case 0xD: {  // SUBA / CMPA / ADDA
        uint32_t s = opm == 3 ? uint32_t(int32_t(int16_t(read_ea<2>(c, mode, reg))))
                              : read_ea<4>(c, mode, reg);
        if (grp == 0x9) c.a[dn] -= s;
        else if (grp == 0xD) c.a[dn] += s;
        else cmp<4>(c, s, c.a[dn]);
        c.cycles += 4;
        return;
      }
    }
    illegal(c);
    return;
  }

  int sz = opm & 3;
  bool to_ea = opm & 4;

  if (to_ea && (mode == 0 || mode == 1)) {
    switch (grp) {
      case 0x8: case 0xC:
        if (sz == 0 && (grp == 0x8 || grp == 0xC)) {  // SBCD / ABCD
          xop<1>(c, grp == 0x8 ? 0x8 : 0xC, mode == 1, dn, reg);
          return;
        }
        if (grp == 0xC) {  // EXG
          if (opm == 5 && mode == 0) { uint32_t t = c.d[dn]; c.d[dn] = c.d[reg]; c.d[reg] = t; return; }
          if (opm == 5 && mode == 1) { uint32_t t = c.a[dn]; c.a[dn] = c.a[reg]; c.a[reg] = t; return; }
          if (opm == 6 && mode == 1) { uint32_t t = c.d[dn]; c.d[dn] = c.a[reg]; c.a[reg] = t; return; }
        }
        illegal(c);
        return;
      case 0x9: case 0xD:  // SUBX / ADDX
        if (sz == 0) xop<1>(c, grp, mode == 1, dn, reg);
        else if (sz == 1) xop<2>(c, grp, mode == 1, dn, reg);
        else xop<4>(c, grp, mode == 1, dn, reg);
        return;
      case 0xB:
        if (mode == 1) {  // CMPM
          auto cmpm = [&](auto szc) {
            constexpr int S = decltype(szc)::value;
            uint32_t s = read_ea<S>(c, 3, reg);
            uint32_t d = read_ea<S>(c, 3, dn);
            cmp<S>(c, s, d);
          };
          if (sz == 0) cmpm(std::integral_constant<int, 1>());
          else if (sz == 1) cmpm(std::integral_constant<int, 2>());
          else cmpm(std::integral_constant<int, 4>());
          return;
        }
        break;  // EOR Dn,Dn handled below
    }
  }
  if (grp == 0xB && !to_ea) {
    // CMP <ea>,Dn
  }
  switch (sz) {
    case 0: arith<1>(c, grp, to_ea, dn, mode, reg); break;
    case 1: arith<2>(c, grp, to_ea, dn, mode, reg); break;
    case 2: arith<4>(c, grp, to_ea, dn, mode, reg); break;
  }
}

// ---- group E: shifts --------------------------------------------------------
template <int S>
uint32_t shift(Cpu& c, int type, bool left, uint32_t v, unsigned n) {
  switch (type) {
    case 0: return left ? asl<S>(c, v, n) : asr<S>(c, v, n);
    case 1: return left ? lsl<S>(c, v, n) : lsr<S>(c, v, n);
    case 2: return left ? roxl<S>(c, v, n) : roxr<S>(c, v, n);
    default: return left ? rol<S>(c, v, n) : ror<S>(c, v, n);
  }
}

void groupE(Cpu& c, uint16_t op) {
  int sz = (op >> 6) & 3;
  bool left = op & 0x100;
  if (sz == 3) {  // memory, shift by 1
    int type = (op >> 9) & 3;
    Ea e = resolve<2>(c, (op >> 3) & 7, op & 7);
    uint32_t v = rd<2>(c, e);
    wr<2>(c, e, shift<2>(c, type, left, v, 1));
    return;
  }
  int type = (op >> 3) & 3;
  int reg = op & 7;
  unsigned n = (op >> 9) & 7;
  if (op & 0x20) n = c.d[n] & 63;
  else if (n == 0) n = 8;
  c.cycles += 2 + 2 * n;
  switch (sz) {
    case 0: set_dreg<1>(c, reg, shift<1>(c, type, left, c.d[reg], n)); break;
    case 1: set_dreg<2>(c, reg, shift<2>(c, type, left, c.d[reg], n)); break;
    case 2: c.d[reg] = shift<4>(c, type, left, c.d[reg], n); break;
  }
}

}  // namespace

void Cpu::exception(int vector) {
  uint16_t old = sr();
  if (!s) { uint32_t tmp = a[7]; a[7] = osp; osp = tmp; s = true; }
  t = false;
  push32(pc);
  push16(old);
  uint32_t v = bus->read32(uint32_t(vector) * 4);
  if (v == 0 && fatal) { fatal(*this, vector); return; }
  pc = v;
  cycles += 34;
}

bool Cpu::interrupt(int level) {
  if (level <= imask && level != 7) return false;
  stopped = false;
  exception(24 + level);
  imask = uint8_t(level);
  return true;
}

void Cpu::step() {
  if (stopped) { cycles += 4; return; }
  op_pc = pc;
  uint16_t op = fetch16();
  switch (op >> 12) {
    case 0x0: group0(*this, op); break;
    case 0x1: move<1>(*this, op); break;
    case 0x2: move<4>(*this, op); break;
    case 0x3: move<2>(*this, op); break;
    case 0x4: group4(*this, op); break;
    case 0x5: group5(*this, op); break;
    case 0x6: group6(*this, op); break;
    case 0x7:
      if (op & 0x100) { illegal(*this); break; }
      d[(op >> 9) & 7] = logic<4>(*this, uint32_t(int32_t(int8_t(op & 0xFF))));
      break;
    case 0xA:
      if (!(line_a && line_a(*this, op))) { pc = op_pc; exception(10); }
      break;
    case 0xF:
      if (!(line_a && line_a(*this, op))) { pc = op_pc; exception(11); }
      break;
    case 0xE: groupE(*this, op); break;
    default: group_arith(*this, op); break;
  }
}

void Cpu::run(int64_t until) {
  while (cycles < until && !stopped) step();
}

}  // namespace m68k
