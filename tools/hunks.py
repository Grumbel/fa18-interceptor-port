#!/usr/bin/env python3
"""Amiga hunk executable parser (HUNK_HEADER / CODE / DATA / BSS / RELOC32 /
SYMBOL / DEBUG / END).  Library module used by the other tools; run directly
to print the hunk table.

    python3 tools/hunks.py assets/F-18_Interceptor_amiga
"""
import struct
import sys

HUNK_UNIT, HUNK_NAME, HUNK_CODE, HUNK_DATA, HUNK_BSS = 0x3E7, 0x3E8, 0x3E9, 0x3EA, 0x3EB
HUNK_RELOC32, HUNK_RELOC16, HUNK_RELOC8 = 0x3EC, 0x3ED, 0x3EE
HUNK_EXT, HUNK_SYMBOL, HUNK_DEBUG, HUNK_END = 0x3EF, 0x3F0, 0x3F1, 0x3F2
HUNK_HEADER, HUNK_OVERLAY, HUNK_BREAK = 0x3F3, 0x3F5, 0x3F6
HUNK_DREL32, HUNK_RELOC32SHORT = 0x3F7, 0x3FC

TYPE_NAMES = {HUNK_CODE: "CODE", HUNK_DATA: "DATA", HUNK_BSS: "BSS"}


class Hunk:
    def __init__(self, index, kind, alloc_size, mem):
        self.index = index
        self.kind = kind            # "CODE" / "DATA" / "BSS"
        self.alloc_size = alloc_size  # bytes
        self.mem = mem              # "any" / "chip" / "fast"
        self.data = b""
        self.file_offset = None     # offset of payload in file
        self.relocs = {}            # target hunk -> [offsets]
        self.symbols = []           # (name, offset)
        self.debug = []

    def __repr__(self):
        return f"<Hunk {self.index} {self.kind} {self.alloc_size:#x} {self.mem}>"


def parse(path):
    raw = open(path, "rb").read()
    pos = 0

    def u32():
        nonlocal pos
        v = struct.unpack_from(">I", raw, pos)[0]
        pos += 4
        return v

    if u32() != HUNK_HEADER:
        raise ValueError("not a hunk executable")
    while True:  # resident library names
        n = u32()
        if n == 0:
            break
        pos += n * 4
    table_size = u32()
    first = u32()
    last = u32()
    hunks = []
    for i in range(first, last + 1):
        v = u32()
        flags = v >> 30
        size = (v & 0x3FFFFFFF) * 4
        if flags == 3:
            u32()
        hunks.append(Hunk(i, None, size, {0: "any", 1: "chip", 2: "fast", 3: "ext"}[flags]))

    cur = 0
    h = None
    while pos < len(raw):
        t = u32() & 0x3FFFFFFF
        if t in (HUNK_CODE, HUNK_DATA, HUNK_BSS):
            h = hunks[cur]
            h.kind = TYPE_NAMES[t]
            n = u32() * 4
            if t == HUNK_BSS:
                h.data = bytes(h.alloc_size)
            else:
                h.file_offset = pos
                h.data = raw[pos:pos + n] + bytes(max(0, h.alloc_size - n))
                pos += n
        elif t == HUNK_RELOC32:
            while True:
                n = u32()
                if n == 0:
                    break
                target = u32()
                offs = struct.unpack_from(f">{n}I", raw, pos)
                pos += 4 * n
                h.relocs.setdefault(target, []).extend(offs)
        elif t == HUNK_RELOC32SHORT:
            while True:
                n = struct.unpack_from(">H", raw, pos)[0]; pos += 2
                if n == 0:
                    break
                target = struct.unpack_from(">H", raw, pos)[0]; pos += 2
                offs = struct.unpack_from(f">{n}H", raw, pos); pos += 2 * n
                h.relocs.setdefault(target, []).extend(offs)
            if pos & 2:
                pos += 2
        elif t == HUNK_SYMBOL:
            while True:
                n = u32()
                if n == 0:
                    break
                name = raw[pos:pos + n * 4].rstrip(b"\0").decode("latin-1")
                pos += n * 4
                h.symbols.append((name, u32()))
        elif t == HUNK_DEBUG:
            n = u32() * 4
            h.debug.append(raw[pos:pos + n])
            pos += n
        elif t == HUNK_NAME:
            pos += u32() * 4
        elif t == HUNK_END:
            cur += 1
        else:
            raise ValueError(f"unknown hunk type {t:#x} at {pos-4:#x}")
    return hunks


if __name__ == "__main__":
    hunks = parse(sys.argv[1] if len(sys.argv) > 1 else "assets/F-18_Interceptor_amiga")
    total = 0
    for h in hunks:
        nrel = sum(len(v) for v in h.relocs.values())
        fo = f"{h.file_offset:#08x}" if h.file_offset is not None else "   -    "
        print(f"{h.index:3} {h.kind:4} {h.alloc_size:7} file={fo} relocs={nrel:5} "
              f"targets={sorted(h.relocs)} syms={len(h.symbols)} dbg={len(h.debug)}")
        total += h.alloc_size
    print("total alloc", total)
