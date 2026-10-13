#!/usr/bin/env python3
"""Relocate all hunks of the executable to fixed addresses, the same way the
C++ runtime's LoadSeg does (keep the two in sync!).

Each hunk gets an 8-byte LoadSeg header (alloc size + 8, next-segment BPTR)
and the segments are laid out back to back, so a WHDLoad PatchSeg offset X is
at address LOAD_BASE + X.

    python3 tools/loadseg.py "F-18 Interceptor" image.bin   # writes flat image
"""
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(__file__))
import hunks as hunkmod

LOAD_BASE = 0x00020000


def layout(hunks, base=LOAD_BASE):
    addrs = []
    a = base
    for h in hunks:
        addrs.append(a + 8)
        a += 8 + h.alloc_size
    return addrs, (a + 7) & ~7


def load(path, base=LOAD_BASE):
    hunks = hunkmod.parse(path)
    addrs, end = layout(hunks, base)
    img = bytearray(end - base)
    for i, h in enumerate(hunks):
        off = addrs[i] - base
        img[off:off + len(h.data)] = h.data
        struct.pack_into(">I", img, off - 8, h.alloc_size + 8)
        nxt = (addrs[i + 1] - 4) >> 2 if i + 1 < len(hunks) else 0
        struct.pack_into(">I", img, off - 4, nxt)
        for target, offs in h.relocs.items():
            for o in offs:
                v = struct.unpack_from(">I", img, off + o)[0]
                struct.pack_into(">I", img, off + o, (v + addrs[target]) & 0xFFFFFFFF)
    return hunks, addrs, img


if __name__ == "__main__":
    hunks, addrs, img = load(sys.argv[1])
    open(sys.argv[2], "wb").write(img)
    for h, a in zip(hunks, addrs):
        print(f"hunk {h.index:3} {h.kind:4} {a:#010x}-{a + h.alloc_size:#010x}")
