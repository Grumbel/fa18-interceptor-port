#!/usr/bin/env python3
"""Parse Amiga HUNK_HEADER and list hunk sizes for FA-18 Interceptor."""
import struct
import sys

def main(path):
    data = open(path, "rb").read()
    if struct.unpack(">I", data[0:4])[0] != 0x3F3:
        print("Not a HUNK_HEADER executable")
        return 1
    residual, table_size, first, last = struct.unpack(">IIII", data[4:20])
    print(f"tableSize={table_size} first={first} last={last}")
    print(f"{'hunk':>6} {'bytes':>8} {'mem':>4}")
    total = 0
    for i in range(table_size):
        raw = struct.unpack(">I", data[20+i*4:24+i*4])[0]
        mem = (raw >> 30) & 3
        sz = (raw & 0x3FFFFFFF) * 4
        if sz == 0:
            continue
        total += sz
        memname = {0: "any", 1: "chip", 2: "fast"}.get(mem, "?")
        print(f"{i:6d} {sz:8d} {memname:>4}")
    print(f"\nNon-zero hunks payload total: {total} bytes")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "assets/F-18_Interceptor_amiga"))
