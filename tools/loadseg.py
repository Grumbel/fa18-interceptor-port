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


def patch_address(hunks, addrs, off):
    """WHDLoad PatchSeg offsets are offsets into the concatenated payloads."""
    for h, a in zip(hunks, addrs):
        if off < h.alloc_size:
            return a + off
        off -= h.alloc_size


def apply_patches(hunks, addrs, img, base=LOAD_BASE):
    """The same patches as Machine::boot (keep in sync):
      $19737 / $3a964  codewheel pass flag preset to 1
      $01a78           6 bytes NOP: the store that clears the flag after
                       the check ($21a80 = moveb d0,$5aba4)
      $008da           delay() busy loop -> host trap A $AF02 (TRAP_DELAY)
    """
    img[patch_address(hunks, addrs, 0x19737) - base] = 0x01
    img[patch_address(hunks, addrs, 0x3a964) - base] = 0x01
    p = patch_address(hunks, addrs, 0x01a78) - base
    img[p:p + 6] = bytes.fromhex("4e714e714e71")
    p = patch_address(hunks, addrs, 0x008da) - base
    img[p:p + 2] = bytes.fromhex("af02")


def load(path, base=LOAD_BASE, patched=True):
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
    if patched:
        apply_patches(hunks, addrs, img, base)
    return hunks, addrs, img


if __name__ == "__main__":
    hunks, addrs, img = load(sys.argv[1])
    open(sys.argv[2], "wb").write(img)
    for h, a in zip(hunks, addrs):
        print(f"hunk {h.index:3} {h.kind:4} {a:#010x}-{a + h.alloc_size:#010x}")
