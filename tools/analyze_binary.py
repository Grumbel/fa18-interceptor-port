#!/usr/bin/env python3
"""Quick static analysis helpers for the FA-18 Interceptor Amiga binary."""
import struct
import sys
from pathlib import Path

LVOs = {
    -552: "OpenLibrary",
    -414: "CloseLibrary",
    -444: "OpenDevice",
    -408: "CloseDevice",
    -198: "AllocMem",
    -210: "FreeMem",
    -108: "Forbid",
    -114: "Permit",
    -132: "Disable",
    -138: "Enable",
    -294: "FindTask",
    -78:  "Wait",
    -456: "DoIO",
    -462: "SendIO",
    -480: "WaitIO",
}

def read_be32(data, o):
    return struct.unpack_from(">I", data, o)[0]

def main():
    path = Path(sys.argv[1] if len(sys.argv) > 1 else "assets/F-18_Interceptor_amiga")
    data = path.read_bytes()
    print(f"File: {path}  size={len(data)}")

    code_hunks = data_hunks = 0
    for i in range(0, len(data)-8, 4):
        t = read_be32(data, i) & 0xFFFF
        if t == 0x3E9:
            sz = read_be32(data, i+4)
            if 0 < sz < 100000: code_hunks += 1
        elif t == 0x3EA:
            sz = read_be32(data, i+4)
            if 0 < sz < 100000: data_hunks += 1
    print(f"CODE hunks (approx): {code_hunks}")
    print(f"DATA hunks (approx): {data_hunks}")

    print("\nOpenLibrary (jsr -552(a6)) sites:")
    for i in range(0, len(data)-4, 2):
        if data[i:i+4] == b"\x4e\xae\xfd\xd8":
            print(f"  0x{i:06x}")

    print("\nOther common LVO calls (sample):")
    found = {}
    for i in range(0, len(data)-4, 2):
        if data[i:i+2] == b"\x4e\xae":
            off = struct.unpack(">h", data[i+2:i+4])[0]
            name = LVOs.get(off, None)
            if name:
                found.setdefault(name, []).append(i)
    for name, addrs in sorted(found.items()):
        print(f"  {name}: {len(addrs)} sites, first @ 0x{addrs[0]:06x}")

    print("\nKey string locations:")
    for s in [b"dos.library", b"graphics.library", b"intuition.library",
              b"F/A-18 INTERCEPTOR", b"YOU ARE NOW OFFICIALLY ENLISTED",
              b"QUALIFICATION", b"SELECTABLE MISSIONS"]:
        idx = data.find(s)
        if idx >= 0:
            print(f"  {s.decode()!r} @ 0x{idx:06x}")

if __name__ == "__main__":
    main()
