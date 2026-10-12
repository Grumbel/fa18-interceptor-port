#!/usr/bin/env python3
"""Map FA-18 Interceptor RAM dumps to the on-disk Amiga binary.

Usage:
  python3 tools/map_ram_dumps.py
  python3 tools/map_ram_dumps.py --verbose

Reads:
  assets/F-18_Interceptor_amiga
  dumps/fa18_{low,code,data,stack}.bin

Prints the primary CODE relocation base, key low-mem vectors, and a
short summary of which overlay hunks are present in the code dump.
"""

from __future__ import annotations

import argparse
import struct
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "assets" / "F-18_Interceptor_amiga"
DUMPS = ROOT / "dumps"


def read_be32(data: bytes, off: int) -> int:
    return struct.unpack_from(">I", data, off)[0]


def find_primary_base(binary: bytes, code: bytes) -> tuple[int, int, int]:
    """Return (dump_start, reloc_base, n_matches).

    dump_start  – offset of primary payload inside fa18_code.bin
    reloc_base  – live address of runtime_r == 0
    """
    # Locate primary by the majority 16-byte match after file 0x300
    file_start = 0x300
    primary_size = 41804
    deltas: list[int] = []
    dump_starts: list[int] = []

    for i in range(file_start, file_start + min(primary_size, 4096), 32):
        seq = binary[i : i + 16]
        if seq.count(0) > 12:
            continue
        idx = code.find(seq)
        if idx >= 0:
            file_rel = i - file_start
            deltas.append(idx - file_rel)
            dump_starts.append(idx - file_rel)

    if not deltas:
        raise SystemExit("Primary CODE not found in fa18_code.bin")

    majority_delta = Counter(deltas).most_common(1)[0][0]
    dump_start = majority_delta  # because file_rel==0 → dump_offset

    # Now recover the absolute relocation base from fixed-up longwords
    pairs: list[int] = []
    for i in range(0, 2048, 2):
        if file_start + i + 4 > len(binary) or dump_start + i + 4 > len(code):
            break
        bval = read_be32(binary, file_start + i)
        dval = read_be32(code, dump_start + i)
        if (bval & 0xFFFF0000) == 0 and (dval & 0xFFFF0000) == 0x00C00000:
            pairs.append(dval - (bval & 0xFFFF))

    if not pairs:
        # fall back: use the secondary base that appears in the dump
        reloc_base = 0x00C06A70  # known from this session
    else:
        reloc_base = Counter(pairs).most_common(1)[0][0]

    n_matches = sum(1 for d in deltas if d == majority_delta)
    return dump_start, reloc_base, n_matches


def low_mem_report(low: bytes) -> None:
    slots = [
        (0x0004, "ExecBase"),
        (0x0012, "draw vector $12"),
        (0x00BE, "$be"),
        (0x00C6, "$c6"),
        (0x05EE, "$5ee"),
        (0x036E, "DrawMode"),
        (0x04B0, "input axes"),
        (0x052B, "shared flag"),
        (0x0642, "object pool"),
        (0x0B8F, "object count (byte)"),
        (0x0BE6, "matrix $be6"),
    ]
    print("Low-memory slots:")
    for addr, name in slots:
        if addr + 4 <= len(low) and addr % 2 == 0:
            v = read_be32(low, addr)
            print(f"  ${addr:04X}  {name:22s}  {v:08X}")
        elif addr < len(low):
            print(f"  ${addr:04X}  {name:22s}  byte {low[addr]:02X}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if not BINARY.exists():
        print(f"Missing {BINARY}", file=sys.stderr)
        return 1
    for name in ("fa18_low.bin", "fa18_code.bin", "fa18_data.bin", "fa18_stack.bin"):
        if not (DUMPS / name).exists():
            print(f"Missing {DUMPS / name}", file=sys.stderr)
            return 1

    binary = BINARY.read_bytes()
    code = (DUMPS / "fa18_code.bin").read_bytes()
    low = (DUMPS / "fa18_low.bin").read_bytes()
    data = (DUMPS / "fa18_data.bin").read_bytes()
    stack = (DUMPS / "fa18_stack.bin").read_bytes()

    dump_start, reloc_base, n_matches = find_primary_base(binary, code)

    print("=== Primary CODE mapping ===")
    print(f"  dump start of primary : 0x{dump_start:X}")
    print(f"  relocation base       : 0x{reloc_base:08X}")
    print(f"  sequence matches      : {n_matches}")
    print()
    print("Formulas:")
    print(f"  live_addr   = 0x{reloc_base:08X} + runtime_r")
    print(f"  dump_offset = 0x{dump_start:X} + runtime_r")
    print(f"  file_offset = 0x300 + runtime_r")
    print()

    low_mem_report(low)
    print()

    # Quick presence check for a few known overlays
    print("Selected overlay presence (12-byte non-zero runs):")
    for off, label in [
        (0x01240C, "modes / $c6 install"),
        (0x01C1D4, "DDA/span"),
        (0x02826C, "geometry"),
        (0x04BF0C, "action create"),
    ]:
        found = False
        for skip in range(0, 64, 4):
            seq = binary[off + skip : off + skip + 12]
            if seq.count(0) >= 8:
                continue
            idx = code.find(seq)
            if idx >= 0:
                print(f"  {label:24s} file 0x{off:X} → dump 0x{idx:X}")
                found = True
                break
        if not found:
            print(f"  {label:24s} not resident (or fully relocated)")

    if args.verbose:
        print()
        print("First 8 stack longs (BE):")
        for i in range(0, min(32, len(stack)), 4):
            print(f"  +{i:02X}: {read_be32(stack, i):08X}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
