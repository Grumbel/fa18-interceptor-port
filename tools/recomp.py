#!/usr/bin/env python3
"""Static recompiler: 68000 game code -> C++.

    python3 tools/recomp.py "assets/F-18 Interceptor" outdir [--seeds file]...

Loads the executable exactly like the runtime (tools/loadseg.py layout plus
the protection patch), discovers code by recursive descent from the entry
point, relocated code pointers and recorded coverage seeds, and emits one
C++ function per chunk of instructions.  The generated code reproduces the
interpreter (src/m68k/cpu.cpp) instruction by instruction, including its
cycle accounting, so the two can be compared in lockstep.  Anything the
translator does not handle natively is delegated to the interpreter
(`c.step()`) for that single instruction.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import loadseg  # noqa: E402

CHUNK_SIZE = 600   # instructions per generated function
FILES = 16         # number of generated .cpp files

MASK = {1: "0xFFu", 2: "0xFFFFu", 4: "0xFFFFFFFFu"}
SUF = {1: "8", 2: "16", 4: "32"}


class Unsupported(Exception):
    pass


class Invalid(Exception):
    pass


def s8(v):
    return v - 0x100 if v & 0x80 else v


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def hx(v):
    return f"0x{v & 0xFFFFFFFF:X}u"


class Ea:
    """Resolved effective address for code generation."""

    def __init__(self, kind, reg=0, expr=None, value=None):
        self.kind = kind    # 'd', 'a', 'm' (memory, expr = address var/const), 'i' (immediate)
        self.reg = reg
        self.expr = expr
        self.value = value


class Insn:
    def __init__(self, addr):
        self.addr = addr
        self.pos = addr + 2
        self.lines = []
        self.pending = 4      # opcode fetch
        self.tmp = 0
        self.succ = []        # static successors (fallthrough / branch targets)
        self.fallthrough = True
        self.calls = []
        self.native = True
        self.text = ""

    # -- extension words ------------------------------------------------
    def fetch16(self):
        v = IMG.word(self.pos)
        self.pos += 2
        self.pending += 4
        return v

    def fetch32(self):
        v = IMG.long(self.pos)
        self.pos += 4
        self.pending += 8
        return v

    def flush(self):
        if self.pending:
            self.lines.append(f"c.cycles += {self.pending};")
            self.pending = 0

    def emit(self, s):
        self.lines.append(s)

    def var(self, prefix="t"):
        self.tmp += 1
        return f"{prefix}{self.tmp}"

    # -- EA resolution (mirror of resolve<S>) ---------------------------
    def index_expr(self, ext):
        r = (ext >> 12) & 7
        x = f"c.a[{r}]" if ext & 0x8000 else f"c.d[{r}]"
        if not ext & 0x800:
            x = f"uint32_t(int32_t(int16_t({x})))"
        return x

    def resolve(self, mode, reg, size):
        if mode == 0:
            return Ea('d', reg)
        if mode == 1:
            return Ea('a', reg)
        v = self.var("ea")
        if mode == 2:
            self.emit(f"uint32_t {v} = c.a[{reg}];")
        elif mode == 3:
            inc = 2 if size == 1 and reg == 7 else size
            self.emit(f"uint32_t {v} = c.a[{reg}]; c.a[{reg}] += {inc};")
        elif mode == 4:
            inc = 2 if size == 1 and reg == 7 else size
            self.pending += 2
            self.emit(f"c.a[{reg}] -= {inc}; uint32_t {v} = c.a[{reg}];")
        elif mode == 5:
            d = s16(self.fetch16())
            self.emit(f"uint32_t {v} = c.a[{reg}] + {hx(d)};")
        elif mode == 6:
            ext = self.fetch16()
            self.pending += 2
            self.emit(f"uint32_t {v} = c.a[{reg}] + {hx(s8(ext & 0xFF))} + {self.index_expr(ext)};")
        elif mode == 7:
            if reg == 0:
                return Ea('m', expr=hx(s16(self.fetch16())))
            if reg == 1:
                return Ea('m', expr=hx(self.fetch32()))
            if reg == 2:
                base = self.pos
                return Ea('m', expr=hx(base + s16(self.fetch16())))
            if reg == 3:
                base = self.pos
                ext = self.fetch16()
                self.pending += 2
                self.emit(f"uint32_t {v} = {hx(base + s8(ext & 0xFF))} + {self.index_expr(ext)};")
                return Ea('m', expr=v)
            if reg == 4:
                if size == 4:
                    return Ea('i', value=self.fetch32())
                if size == 2:
                    return Ea('i', value=self.fetch16())
                return Ea('i', value=self.fetch16() & 0xFF)
            raise Invalid()
        return Ea('m', expr=v)

    def rd(self, e, size):
        if e.kind == 'd':
            return f"(c.d[{e.reg}] & {MASK[size]})" if size != 4 else f"c.d[{e.reg}]"
        if e.kind == 'a':
            return f"(c.a[{e.reg}] & {MASK[size]})" if size != 4 else f"c.a[{e.reg}]"
        if e.kind == 'i':
            return hx(e.value)
        self.flush()
        v = self.var("v")
        self.emit(f"uint32_t {v} = rd{SUF[size]}(c, {e.expr});")
        return v

    def wr(self, e, size, val):
        if e.kind == 'd':
            if size == 4:
                self.emit(f"c.d[{e.reg}] = {val};")
            else:
                self.emit(f"c.d[{e.reg}] = (c.d[{e.reg}] & ~{MASK[size]}) | (({val}) & {MASK[size]});")
        elif e.kind == 'a':
            if size == 2:
                self.emit(f"c.a[{e.reg}] = uint32_t(int32_t(int16_t({val})));")
            else:
                self.emit(f"c.a[{e.reg}] = {val};")
        elif e.kind == 'i':
            pass
        else:
            self.flush()
            self.emit(f"wr{SUF[size]}(c, {e.expr}, {val});")

    def read_ea(self, mode, reg, size):
        e = self.resolve(mode, reg, size)
        return self.rd(e, size)

    def ctrl_addr(self, mode, reg):
        if mode in (0, 1, 3, 4) or (mode == 7 and reg == 4):
            raise Invalid()
        e = self.resolve(mode, reg, 4)
        return e.expr

    def set_dreg(self, size, r, val):
        self.wr(Ea('d', r), size, val)


class Image:
    def __init__(self, img, base):
        self.img = img
        self.base = base

    def word(self, a):
        o = a - self.base
        return struct.unpack_from(">H", self.img, o)[0]

    def long(self, a):
        o = a - self.base
        return struct.unpack_from(">I", self.img, o)[0]


IMG = None
CODE_RANGES = []


def in_code(a):
    for lo, hi in CODE_RANGES:
        if lo <= a < hi:
            return True
    return False


# ---------------------------------------------------------------------------
# Instruction translation.  Each function mirrors the corresponding part of
# src/m68k/cpu.cpp; keep them in sync.
# ---------------------------------------------------------------------------

def fallback(i, op):
    """Let the interpreter execute this one instruction."""
    i.native = False
    i.lines = ["c.pc = %s; c.step(); return;" % hx(i.addr)]
    i.pending = 0


def sz_of(bits):
    return {0: 1, 1: 2, 2: 4}[bits]


def t_imm_op(i, op, kind, size, mode, reg):
    if size == 4:
        imm = i.fetch32()
    elif size == 2:
        imm = i.fetch16()
    else:
        imm = i.fetch16() & 0xFF
    e = i.resolve(mode, reg, size)
    if mode == 1 or (mode == 7 and reg > 1 and kind != 6):
        raise Invalid()
    d = i.rd(e, size)
    S = size
    if kind == 0:
        i.wr(e, S, f"logic<{S}>(c, {d} | {hx(imm)})")
    elif kind == 1:
        i.wr(e, S, f"logic<{S}>(c, {d} & {hx(imm)})")
    elif kind == 2:
        i.wr(e, S, f"sub<{S}>(c, {hx(imm)}, {d})")
    elif kind == 3:
        i.wr(e, S, f"add<{S}>(c, {hx(imm)}, {d})")
    elif kind == 5:
        i.wr(e, S, f"logic<{S}>(c, {d} ^ {hx(imm)})")
    elif kind == 6:
        i.emit(f"cmp<{S}>(c, {hx(imm)}, {d});")
    else:
        raise Invalid()


def t_bit_op(i, kind, bit_expr, mode, reg):
    if mode == 0:
        i.pending += 2
        m = i.var("m")
        i.emit(f"uint32_t {m} = 1u << (({bit_expr}) & 31);")
        i.emit(f"c.fz = !(c.d[{reg}] & {m});")
        if kind == 1:
            i.emit(f"c.d[{reg}] ^= {m};")
        elif kind == 2:
            i.emit(f"c.d[{reg}] &= ~{m};")
        elif kind == 3:
            i.emit(f"c.d[{reg}] |= {m};")
        return
    if mode == 1:
        raise Invalid()
    e = i.resolve(mode, reg, 1)
    v = i.rd(e, 1)
    m = i.var("m")
    i.emit(f"uint32_t {m} = 1u << (({bit_expr}) & 7);")
    i.emit(f"c.fz = !({v} & {m});")
    if kind == 1:
        i.wr(e, 1, f"{v} ^ {m}")
    elif kind == 2:
        i.wr(e, 1, f"{v} & ~{m}")
    elif kind == 3:
        i.wr(e, 1, f"{v} | {m}")


def t_group0(i, op):
    mode, reg = (op >> 3) & 7, op & 7
    if op & 0x100:
        dn = (op >> 9) & 7
        if mode == 1:
            fallback(i, op)  # MOVEP
            i.fetch16()
            return
        t_bit_op(i, (op >> 6) & 3, f"c.d[{dn}]", mode, reg)
        return
    kind = (op >> 9) & 7
    sz = (op >> 6) & 3
    if kind == 4:
        bit = i.fetch16() & 0xFF
        t_bit_op(i, sz, str(bit), mode, reg)
        return
    if kind == 7 or sz == 3:
        raise Invalid()
    if mode == 7 and reg == 4:
        i.fetch16()
        fallback(i, op)  # to CCR/SR
        return
    t_imm_op(i, op, kind, sz_of(sz), mode, reg)


def t_move(i, op, size):
    smode, sreg = (op >> 3) & 7, op & 7
    dmode, dreg = (op >> 6) & 7, (op >> 9) & 7
    if size == 1 and smode == 1:
        raise Invalid()
    v = i.read_ea(smode, sreg, size)
    if dmode == 1:
        if size == 1:
            raise Invalid()
        if size == 2:
            i.emit(f"c.a[{dreg}] = uint32_t(int32_t(int16_t({v})));")
        else:
            i.emit(f"c.a[{dreg}] = {v};")
        return
    if dmode == 7 and dreg > 1:
        raise Invalid()
    t = i.var("mv")
    i.emit(f"uint32_t {t} = {v};")
    e = i.resolve(dmode, dreg, size)
    i.emit(f"logic<{size}>(c, {t});")
    i.wr(e, size, t)


def t_movem(i, op):
    to_reg = bool(op & 0x400)
    lng = bool(op & 0x40)
    mode, reg = (op >> 3) & 7, op & 7
    mask = i.fetch16()
    sz = 4 if lng else 2
    per = 8 if lng else 4
    if not to_reg and mode == 4:
        a = i.var("ad")
        o = i.var("orig")
        i.emit(f"uint32_t {a} = c.a[{reg}]; uint32_t {o} = c.a[{reg}]; (void){o};")
        for b in range(16):
            if not mask & (1 << b):
                continue
            r = 15 - b
            src = f"c.d[{r}]" if r < 8 else (o if r - 8 == reg else f"c.a[{r - 8}]")
            i.emit(f"{a} -= {sz};")
            if lng:
                i.emit(f"c.bus->write32({a}, {src});")
            else:
                i.emit(f"c.bus->write16({a}, uint16_t({src}));")
            i.pending += per
        i.emit(f"c.a[{reg}] = {a};")
        return
    if mode == 3:
        a = i.var("ad")
        i.emit(f"uint32_t {a} = c.a[{reg}];")
    else:
        if mode in (0, 1, 4) or (mode == 7 and reg == 4):
            raise Invalid()
        if not to_reg and mode == 3:
            raise Invalid()
        e = i.ctrl_addr(mode, reg)
        a = i.var("ad")
        i.emit(f"uint32_t {a} = {e};")
    i.flush()
    for b in range(16):
        if not mask & (1 << b):
            continue
        dst = f"c.d[{b}]" if b < 8 else f"c.a[{b - 8}]"
        if to_reg:
            if lng:
                i.emit(f"{dst} = c.bus->read32({a});")
            else:
                i.emit(f"{dst} = uint32_t(int32_t(int16_t(c.bus->read16({a}))));")
        else:
            if lng:
                i.emit(f"c.bus->write32({a}, {dst});")
            else:
                i.emit(f"c.bus->write16({a}, uint16_t({dst}));")
        i.emit(f"{a} += {sz};")
        i.pending += per
    if to_reg:
        i.pending += 4
    if mode == 3:
        i.emit(f"c.a[{reg}] = {a};")


def t_unary(i, kind, size, mode, reg):
    if mode == 1 or (mode == 7 and reg > 1 and kind != 5):
        raise Invalid()
    if mode == 7 and reg == 4:
        raise Invalid()
    e = i.resolve(mode, reg, size)
    S = size
    if kind == 0:
        i.wr(e, S, f"negx<{S}>(c, {i.rd(e, S)})")
    elif kind == 1:
        if e.kind == 'm':
            i.rd(e, S)
        i.wr(e, S, f"logic<{S}>(c, 0)")
    elif kind == 2:
        i.wr(e, S, f"neg<{S}>(c, {i.rd(e, S)})")
    elif kind == 3:
        i.wr(e, S, f"logic<{S}>(c, ~{i.rd(e, S)})")
    elif kind == 5:
        i.emit(f"logic<{S}>(c, {i.rd(e, S)});")


def t_group4(i, op):
    mode, reg = (op >> 3) & 7, op & 7
    if op & 0x100:
        an = (op >> 9) & 7
        if (op & 0x1C0) == 0x1C0:  # LEA
            i.emit(f"c.a[{an}] = {i.ctrl_addr(mode, reg)};")
            return
        if (op & 0x1C0) == 0x180:  # CHK
            i.resolve(mode, reg, 2)
            fallback(i, op)
            return
        raise Invalid()
    sub = (op >> 8) & 0xF
    sz = (op >> 6) & 3
    if sub == 0x0:
        if sz == 3:  # MOVE from SR
            e = i.resolve(mode, reg, 2)
            i.wr(e, 2, "c.sr()")
        else:
            t_unary(i, 0, sz_of(sz), mode, reg)
        return
    if sub == 0x2:
        if sz == 3:
            raise Invalid()
        t_unary(i, 1, sz_of(sz), mode, reg)
        return
    if sub == 0x4:
        if sz == 3:  # MOVE to CCR
            v = i.read_ea(mode, reg, 2)
            i.emit(f"c.set_ccr(uint16_t({v}));")
            return
        t_unary(i, 2, sz_of(sz), mode, reg)
        return
    if sub == 0x6:
        if sz == 3:  # MOVE to SR (privileged)
            i.resolve(mode, reg, 2)
            fallback(i, op)
            return
        t_unary(i, 3, sz_of(sz), mode, reg)
        return
    if sub == 0x8:
        if sz == 0:  # NBCD
            e = i.resolve(mode, reg, 1)
            i.wr(e, 1, f"sbcd(c, {i.rd(e, 1)}, 0)")
            return
        if sz == 1:
            if mode == 0:  # SWAP
                i.emit(f"c.d[{reg}] = logic<4>(c, (c.d[{reg}] >> 16) | (c.d[{reg}] << 16));")
            else:  # PEA
                a = i.ctrl_addr(mode, reg)
                i.emit(f"c.push32({a});")
            return
        if mode == 0:  # EXT
            if sz == 2:
                i.set_dreg(2, reg, f"logic<2>(c, uint32_t(int32_t(int8_t(c.d[{reg}]))))")
            else:
                i.emit(f"c.d[{reg}] = logic<4>(c, uint32_t(int32_t(int16_t(c.d[{reg}]))));")
            return
        t_movem(i, op)
        return
    if sub == 0xA:
        if sz == 3:
            if op == 0x4AFC:
                raise Invalid()
            e = i.resolve(mode, reg, 1)  # TAS
            v = i.rd(e, 1)
            i.emit(f"logic<1>(c, {v});")
            i.wr(e, 1, f"{v} | 0x80u")
            return
        t_unary(i, 5, sz_of(sz), mode, reg)
        return
    if sub == 0xC:
        if sz >= 2:
            t_movem(i, op)
            return
        raise Invalid()
    if sub == 0xE:
        if sz == 1:
            k = (op >> 3) & 7
            if k in (0, 1):  # TRAP
                fallback(i, op)
                i.fallthrough = True
                return
            if k == 2:  # LINK
                d = s16(i.fetch16())
                i.emit(f"c.push32(c.a[{reg}]); c.a[{reg}] = c.a[7]; c.a[7] += {hx(d)};")
                return
            if k == 3:  # UNLK
                i.emit(f"c.a[7] = c.a[{reg}]; c.a[{reg}] = c.pop32();")
                return
            if k in (4, 5):  # MOVE USP
                fallback(i, op)
                return
            if k == 6:
                low = op & 7
                if low == 1:  # NOP
                    return
                if low == 5:  # RTS
                    i.flush()
                    i.emit("c.pc = c.pop32(); return;")
                    i.fallthrough = False
                    return
                if low == 7:  # RTR
                    i.flush()
                    i.emit("c.set_ccr(c.pop16()); c.pc = c.pop32(); return;")
                    i.fallthrough = False
                    return
                if low == 2:  # STOP
                    i.fetch16()
                    fallback(i, op)
                    return
                if low == 3:  # RTE
                    fallback(i, op)
                    i.fallthrough = False
                    return
                if low in (0, 6):  # RESET / TRAPV
                    fallback(i, op)
                    return
                raise Invalid()
            raise Invalid()
        if sz == 2:  # JSR
            tgt = jump_target(i, mode, reg)
            ret = i.pos
            i.emit(f"c.push32({hx(ret)});")
            i.calls.append(tgt)
            i.branch_to(tgt)
            i.succ.append(ret)
            i.fallthrough = False
            return
        if sz == 3:  # JMP
            if mode == 7 and reg == 3:
                # C switch idiom: jmp (pc,dN) into a table of BRA instructions.
                pos = i.pos + 2
                for _ in range(256):
                    w = IMG.word(pos)
                    if w & 0xFF00 != 0x6000:
                        break
                    i.succ.append(pos)
                    pos += 4 if w & 0xFF == 0 else 2
            tgt = jump_target(i, mode, reg)
            i.branch_to(tgt)
            i.fallthrough = False
            return
    raise Invalid()


def jump_target(i, mode, reg):
    """Returns a constant target address or a C++ expression string."""
    if mode == 7 and reg in (0, 1, 2):
        e = i.resolve(mode, reg, 4)
        return int(e.expr.rstrip("u"), 16)
    return i.ctrl_addr(mode, reg)


def t_group5(i, op):
    mode, reg = (op >> 3) & 7, op & 7
    sz = (op >> 6) & 3
    if sz == 3:
        cc = (op >> 8) & 15
        if mode == 1:  # DBcc
            disp = s16(i.fetch16())
            tgt = i.addr + 2 + disp
            i.flush()
            i.emit(f"if (!c.cond({cc})) {{")
            i.emit(f"  uint16_t dv = uint16_t(c.d[{reg}]) - 1;")
            i.emit(f"  c.d[{reg}] = (c.d[{reg}] & ~0xFFFFu) | dv;")
            i.emit("  if (dv != 0xFFFF) {")
            i.branch_lines(tgt, indent="    ")
            i.emit("  }")
            i.emit("}")
            i.succ.append(tgt)
            return
        if mode == 7 and reg > 1:
            raise Invalid()
        e = i.resolve(mode, reg, 1)
        i.wr(e, 1, f"(c.cond({cc}) ? 0xFFu : 0u)")
        return
    q = (op >> 9) & 7 or 8
    is_sub = bool(op & 0x100)
    S = sz_of(sz)
    if mode == 1:
        if S == 1:
            raise Invalid()
        i.emit(f"c.a[{reg}] {'-' if is_sub else '+'}= {q};")
        return
    if mode == 7 and reg > 1:
        raise Invalid()
    e = i.resolve(mode, reg, S)
    d = i.rd(e, S)
    i.wr(e, S, f"{'sub' if is_sub else 'add'}<{S}>(c, {q}, {d})")


def t_group6(i, op):
    cc = (op >> 8) & 15
    base = i.addr + 2
    disp = s8(op & 0xFF)
    if disp == 0:
        disp = s16(i.fetch16())
    elif (op & 0xFF) == 0xFF:
        pass  # 68020 32-bit displacement; on 68000 it is a short branch of -1
    tgt = base + disp
    if cc == 1:  # BSR
        ret = i.pos
        i.emit(f"c.push32({hx(ret)});")
        i.calls.append(tgt)
        i.branch_to(tgt)
        i.succ.append(ret)
        i.fallthrough = False
        return
    if cc == 0:  # BRA
        i.pending += 2
        i.branch_to(tgt)
        i.fallthrough = False
        return
    i.flush()
    i.emit(f"if (c.cond({cc})) {{")
    i.emit("  c.cycles += 2;")
    i.branch_lines(tgt, indent="  ")
    i.emit("}")
    i.succ.append(tgt)


def t_moveq(i, op):
    if op & 0x100:
        raise Invalid()
    v = s8(op & 0xFF)
    i.emit(f"c.d[{(op >> 9) & 7}] = logic<4>(c, {hx(v)});")


def t_arith(i, op):
    grp = op >> 12
    dn = (op >> 9) & 7
    opm = (op >> 6) & 7
    mode, reg = (op >> 3) & 7, op & 7
    if opm in (3, 7):
        if grp == 0x8:  # DIVU/DIVS
            s = i.read_ea(mode, reg, 2)
            i.pending += 136
            i.flush()
            fn = "divu" if opm == 3 else "divs"
            i.emit(f"if (!{fn}(c, {s}, c.d[{dn}])) {{ c.pc = {hx(i.pos)}; c.exception(5); return; }}")
            return
        if grp == 0xC:  # MULU/MULS
            s = i.read_ea(mode, reg, 2)
            fn = "mulu" if opm == 3 else "muls"
            i.emit(f"c.d[{dn}] = {fn}(c, {s}, c.d[{dn}]);")
            i.pending += 50
            return
        if grp in (0x9, 0xB, 0xD):
            if opm == 3:
                s = i.read_ea(mode, reg, 2)
                s = f"uint32_t(int32_t(int16_t({s})))"
            else:
                s = i.read_ea(mode, reg, 4)
            if grp == 0x9:
                i.emit(f"c.a[{dn}] -= {s};")
            elif grp == 0xD:
                i.emit(f"c.a[{dn}] += {s};")
            else:
                i.emit(f"cmp<4>(c, {s}, c.a[{dn}]);")
            i.pending += 4
            return
        raise Invalid()
    sz = opm & 3
    to_ea = bool(opm & 4)
    S = sz_of(sz)
    if to_ea and mode in (0, 1):
        if grp in (0x8, 0xC):
            if sz == 0:  # SBCD / ABCD
                t_xop(i, grp, mode == 1, dn, reg, 1)
                return
            if grp == 0xC:  # EXG
                if opm == 5 and mode == 0:
                    i.emit(f"std::swap(c.d[{dn}], c.d[{reg}]);")
                    return
                if opm == 5 and mode == 1:
                    i.emit(f"std::swap(c.a[{dn}], c.a[{reg}]);")
                    return
                if opm == 6 and mode == 1:
                    i.emit(f"std::swap(c.d[{dn}], c.a[{reg}]);")
                    return
            raise Invalid()
        if grp in (0x9, 0xD):  # SUBX/ADDX
            t_xop(i, grp, mode == 1, dn, reg, S)
            return
        if grp == 0xB and mode == 1:  # CMPM
            sv = i.read_ea(3, reg, S)
            dv = i.read_ea(3, dn, S)
            i.emit(f"cmp<{S}>(c, {sv}, {dv});")
            return
    if not to_ea:
        if S == 1 and mode == 1:
            raise Invalid()
        s = i.read_ea(mode, reg, S)
        d = f"(c.d[{dn}] & {MASK[S]})" if S != 4 else f"c.d[{dn}]"
        if grp == 0x8:
            r = f"logic<{S}>(c, {d} | {s})"
        elif grp == 0x9:
            r = f"sub<{S}>(c, {s}, {d})"
        elif grp == 0xB:
            i.emit(f"cmp<{S}>(c, {s}, {d});")
            return
        elif grp == 0xC:
            r = f"logic<{S}>(c, {d} & {s})"
        else:
            r = f"add<{S}>(c, {s}, {d})"
        i.set_dreg(S, dn, r)
        return
    if mode == 7 and reg > 1:
        raise Invalid()
    if mode == 1:
        raise Invalid()
    e = i.resolve(mode, reg, S)
    d = i.rd(e, S)
    s = f"(c.d[{dn}] & {MASK[S]})" if S != 4 else f"c.d[{dn}]"
    if grp == 0x8:
        r = f"logic<{S}>(c, {d} | {s})"
    elif grp == 0x9:
        r = f"sub<{S}>(c, {s}, {d})"
    elif grp == 0xB:
        r = f"logic<{S}>(c, {d} ^ {s})"
    elif grp == 0xC:
        r = f"logic<{S}>(c, {d} & {s})"
    else:
        r = f"add<{S}>(c, {s}, {d})"
    i.wr(e, S, r)


def t_xop(i, grp, mem, rx, ry, S):
    fn = {0xD: f"addx<{S}>", 0x9: f"subx<{S}>", 0xC: "abcd", 0x8: "sbcd"}[grp]
    if not mem:
        s = f"(c.d[{ry}] & {MASK[S]})" if S != 4 else f"c.d[{ry}]"
        d = f"(c.d[{rx}] & {MASK[S]})" if S != 4 else f"c.d[{rx}]"
        i.set_dreg(S, rx, f"{fn}(c, {s}, {d})")
        return
    es = i.resolve(4, ry, S)
    s = i.rd(es, S)
    ed = i.resolve(4, rx, S)
    d = i.rd(ed, S)
    i.wr(ed, S, f"{fn}(c, {s}, {d})")


SHIFT = {(0, False): "asr", (0, True): "asl", (1, False): "lsr", (1, True): "lsl",
         (2, False): "roxr", (2, True): "roxl", (3, False): "ror", (3, True): "rol"}


def t_groupE(i, op):
    sz = (op >> 6) & 3
    left = bool(op & 0x100)
    if sz == 3:
        typ = (op >> 9) & 3
        if op & 0x800:
            raise Invalid()
        mode, reg = (op >> 3) & 7, op & 7
        if mode in (0, 1) or (mode == 7 and reg > 1):
            raise Invalid()
        e = i.resolve(mode, reg, 2)
        v = i.rd(e, 2)
        i.wr(e, 2, f"{SHIFT[(typ, left)]}<2>(c, {v}, 1)")
        return
    typ = (op >> 3) & 3
    reg = op & 7
    n = (op >> 9) & 7
    S = sz_of(sz)
    fn = SHIFT[(typ, left)]
    if op & 0x20:
        cnt = i.var("n")
        i.emit(f"unsigned {cnt} = c.d[{n}] & 63;")
        i.flush()
        i.emit(f"c.cycles += 2 + 2 * {cnt};")
    else:
        cnt = str(n or 8)
        i.pending += 2 + 2 * (n or 8)
    i.set_dreg(S, reg, f"{fn}<{S}>(c, c.d[{reg}], {cnt})")


def translate(addr):
    i = Insn(addr)
    op = IMG.word(addr)

    def branch_lines(tgt, indent=""):
        # Taken control transfer: cycles already accounted.
        if isinstance(tgt, int):
            i.emit(f"{indent}if (c.cycles >= dl) {{ c.pc = {hx(tgt)}; return; }}")
            i.emit(f"{indent}JUMP({hx(tgt)});")
        else:
            i.emit(f"{indent}c.pc = {tgt}; return;")

    def branch_to(tgt):
        i.flush()
        if isinstance(tgt, int):
            i.succ.append(tgt)
            branch_lines(tgt)
        else:
            i.emit(f"c.pc = {tgt}; return;")

    i.branch_lines = branch_lines
    i.branch_to = branch_to
    g = op >> 12
    try:
        if g == 0x0:
            t_group0(i, op)
        elif g == 0x1:
            t_move(i, op, 1)
        elif g == 0x2:
            t_move(i, op, 4)
        elif g == 0x3:
            t_move(i, op, 2)
        elif g == 0x4:
            t_group4(i, op)
        elif g == 0x5:
            t_group5(i, op)
        elif g == 0x6:
            t_group6(i, op)
        elif g == 0x7:
            t_moveq(i, op)
        elif g in (0xA, 0xF):
            raise Invalid()
        elif g == 0xE:
            t_groupE(i, op)
        else:
            t_arith(i, op)
    except (Invalid, struct.error):
        return None
    i.flush()
    i.length = i.pos - addr
    if not i.native:
        # Interpreter executes it; still follow the fallthrough for discovery.
        i.lines = ["c.pc = %s; c.step(); return;" % hx(addr)]
        if i.fallthrough:
            i.succ.append(i.pos)
        i.fallthrough = False
    if i.fallthrough:
        i.succ.append(i.pos)
    return i


# ---------------------------------------------------------------------------

def discover(seeds):
    insns = {}
    work = [s for s in seeds if in_code(s) and not s & 1]
    while work:
        a = work.pop()
        if a in insns or not in_code(a) or a & 1:
            continue
        ins = translate(a)
        if ins is None or not in_code(a + ins.length - 1):
            insns[a] = None
            continue
        insns[a] = ins
        for s in ins.succ:
            if s not in insns:
                work.append(s)
        for s in ins.calls:
            if isinstance(s, int) and s not in insns:
                work.append(s)
    return {a: i for a, i in insns.items() if i is not None}


def fnv(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def main():
    global IMG
    exe = sys.argv[1]
    outdir = sys.argv[2]
    seed_files = []
    func_file = None
    args = sys.argv[3:]
    while args:
        if args[0] == "--seeds":
            seed_files.append(args[1])
            args = args[2:]
        elif args[0] == "--functions":
            func_file = args[1]
            args = args[2:]
        else:
            args = args[1:]
    hunks, addrs, img = loadseg.load(exe)
    base = loadseg.LOAD_BASE

    # Same protection bypass as Machine::boot.
    def patch_address(off):
        for h, a in zip(hunks, addrs):
            if off < h.alloc_size:
                return a + off
            off -= h.alloc_size
    img[patch_address(0x19737) - base] = 0x01
    img[patch_address(0x3a964) - base] = 0x01
    p = patch_address(0x01a78) - base
    img[p:p + 6] = bytes.fromhex("4e714e714e71")
    # delay() busy loop -> host trap (TRAP_DELAY), see Machine::boot
    p = patch_address(0x008da) - base
    img[p:p + 2] = bytes.fromhex("af02")

    IMG = Image(img, base)
    for h, a in zip(hunks, addrs):
        if h.kind == "CODE":
            CODE_RANGES.append((a, a + h.alloc_size))

    seeds = {addrs[0]}
    # Relocated pointers into code hunks may be code pointers.
    for h, a in zip(hunks, addrs):
        for target, offs in h.relocs.items():
            if hunks[target].kind != "CODE":
                continue
            for o in offs:
                v = struct.unpack_from(">I", img, a + o - base)[0]
                seeds.add(v)
    for sf in seed_files:
        for line in open(sf):
            parts = line.split()
            if parts and parts[0] == "x":
                seeds.add(int(parts[1], 16))
            elif parts and not line.startswith("#"):
                try:
                    seeds.add(int(parts[0], 16))
                except ValueError:
                    pass
    insns = discover(seeds)
    order = sorted(insns)
    # Subroutine entry points: targets of JSR/BSR with a constant address.
    entries = {t for i in insns.values() for t in i.calls if isinstance(t, int) and t in insns}
    if func_file:
        with open(func_file, "w") as f:
            for t in sorted(entries):
                f.write(f"{t:06x}\n")
    native = sum(1 for a in order if insns[a].native)
    print(f"recomp: {len(order)} instructions, {native} native", file=sys.stderr)

    # Chunking: contiguous runs within a hunk.
    def hunk_of(a):
        for idx, (lo, hi) in enumerate(CODE_RANGES):
            if lo <= a < hi:
                return idx
        return -1

    chunks = []
    cur = []
    for a in order:
        if cur and (len(cur) >= CHUNK_SIZE or hunk_of(a) != hunk_of(cur[-1])):
            chunks.append(cur)
            cur = []
        cur.append(a)
    if cur:
        chunks.append(cur)
    chunk_of = {}
    for n, ch in enumerate(chunks):
        for a in ch:
            chunk_of[a] = n

    os.makedirs(outdir, exist_ok=True)
    files = [[] for _ in range(FILES)]
    for n, ch in enumerate(chunks):
        files[n % FILES].append(n)

    header = """// Generated by tools/recomp.py from the original executable -- do not edit.
#include "recomp/runtime.hpp"
using namespace m68k;
namespace recomp {
"""
    for fidx, chunk_ids in enumerate(files):
        out = [header]
        for n in chunk_ids:
            ch = chunks[n]
            labels = set(ch)
            out.append(f"void chunk_{n}(Cpu& c, int64_t dl) {{")
            out.append("  switch (c.pc) {")
            for a in ch:
                out.append(f"    case {hx(a)}: goto L_{a:X};")
            out.append("    default: return;")
            out.append("  }")
            for k, a in enumerate(ch):
                ins = insns[a]
                out.append(f"L_{a:X}: {{")
                for line in ins.lines:
                    if "JUMP(" in line:
                        pre, rest = line.split("JUMP(", 1)
                        tgt = int(rest.split(")")[0].rstrip("u"), 16)
                        # Function entries always go through the dispatcher so
                        # native C++ replacements (src/native) take effect.
                        if tgt in labels and tgt not in entries:
                            line = f"{pre}goto L_{tgt:X};"
                        else:
                            line = f"{pre}{{ c.pc = {hx(tgt)}; return; }}"
                    out.append("  " + line)
                out.append("}")
                if ins.fallthrough:
                    nxt = a + ins.length
                    out.append(f"  if (c.cycles >= dl) {{ c.pc = {hx(nxt)}; return; }}")
                    following = ch[k + 1] if k + 1 < len(ch) else None
                    if nxt in entries:
                        out.append(f"  c.pc = {hx(nxt)}; return;")
                    elif following != nxt:
                        if nxt in labels:
                            out.append(f"  goto L_{nxt:X};")
                        else:
                            out.append(f"  c.pc = {hx(nxt)}; return;")
            out.append("}")
            out.append("")
        out.append("}  // namespace recomp")
        with open(os.path.join(outdir, f"code_{fidx:02d}.cpp"), "w") as f:
            f.write("\n".join(out) + "\n")

    # Entry table + image description
    end = max(a + h.alloc_size for h, a in zip(hunks, addrs))
    size = (end - base + 1) & ~1
    out = ['// Generated by tools/recomp.py -- do not edit.',
           '#include "recomp/runtime.hpp"',
           'namespace recomp {']
    for n in range(len(chunks)):
        out.append(f"void chunk_{n}(m68k::Cpu&, int64_t);")
    out.append("static const Entry kEntries[] = {")
    for a in order:
        out.append(f"  {{{hx(a)}, chunk_{chunk_of[a]}}},")
    out.append("};")
    out.append(f"static const Image kImage = {{kEntries, {len(order)}, {hx(base)}, {hx(size)}, "
               f"{hx(fnv(img[:size]))}}};")
    out.append("const Image* image() { return &kImage; }")
    out.append("}  // namespace recomp")
    with open(os.path.join(outdir, "entries.cpp"), "w") as f:
        f.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
