#!/usr/bin/env python3
"""Check the original FA-18 executable's per-plane line-colour operands.

The independently reconstructed Hunk-36 line emitter selects the low byte
of $5AC4E in negative mode, but our native high-resolution observer reads
$5AC4C.  Verify which addresses the *user's own ADF* actually references:

    python3 tools/verify_line_flags.py /path/to/original.adf
    python3 tools/verify_line_flags.py '/path/to/F-18 Interceptor'

Only reads the original executable; no game data or extracted binaries are
retained, and no files are written into the repository.
"""

import argparse
from dataclasses import dataclass
import os
import struct
import tempfile

from adf import Adf
import loadseg


LINE_START = 0x44782
LINE_END = 0x44994  # next known Hunk-36 routine, exclusive
MODE_FLAG_BYTE = 0x5A9E1
NEGATIVE_FLAG_BYTE = 0x5AC4F


@dataclass(frozen=True)
class FlagReference:
    plane: int
    mode: str
    operand: int
    locations: tuple[int, ...]


def btst_immediate_absolute_long(plane, address):
    """68000 encoding of BTST.B #plane,($address).L (8 bytes)."""
    return struct.pack(">HHI", 0x0839, plane, address)


def find_references(image, *, base=loadseg.LOAD_BASE,
                    start=LINE_START, end=LINE_END):
    """Find the eight per-plane BTST operands in a relocated flat image."""
    if start < base or end <= start or end > base + len(image):
        raise ValueError(f"line-emitter window ${start:06X}-${end:06X} "
                         "is outside the loaded executable")
    window = bytes(image[start - base:end - base])
    results = []
    for plane in range(4):
        for mode, address in (("nonnegative", MODE_FLAG_BYTE),
                              ("negative", NEGATIVE_FLAG_BYTE)):
            pattern = btst_immediate_absolute_long(plane, address)
            locations = []
            offset = window.find(pattern)
            while offset != -1:
                locations.append(start + offset)
                offset = window.find(pattern, offset + 1)
            results.append(FlagReference(plane, mode, address, tuple(locations)))
    return results


def matches_reference(results):
    """Require exactly one of each opcode and the reference's plane order."""
    if any(len(ref.locations) != 1 for ref in results):
        return False
    ordered = sorted(results, key=lambda r: r.locations[0])
    expected_order = [(plane, mode)
                      for plane in range(4)
                      for mode in ("nonnegative", "negative")]
    return [(r.plane, r.mode) for r in ordered] == expected_order


def find_other_selectors(image, *, base=loadseg.LOAD_BASE,
                         start=LINE_START, end=LINE_END):
    """Show RAM-operand BTST candidates if this executable is a variant."""
    window = bytes(image[start - base:end - base])
    for off in range(0, len(window) - 7, 2):
        op, bit, address = struct.unpack_from(">HHI", window, off)
        if op == 0x0839 and bit < 4 and 0x50000 <= address < 0x60000:
            if address not in (MODE_FLAG_BYTE, NEGATIVE_FLAG_BYTE):
                yield start + off, bit, address


def relocated_image(path):
    if not path.lower().endswith(".adf"):
        return loadseg.load(path, patched=False)[2]

    disk = Adf(path)
    matches = [(header, size) for name, header, size in disk.walk()
               if name.lower() == "f-18 interceptor" and size is not None]
    if len(matches) != 1:
        raise ValueError(f"expected one 'F-18 Interceptor' executable in ADF, "
                         f"found {len(matches)}")
    executable = disk.read_file(matches[0][0])
    # Reuse the established loadseg parser, without retaining the game binary.
    with tempfile.TemporaryDirectory(prefix="fa18-line-flags-") as directory:
        extracted_path = os.path.join(directory, "F-18 Interceptor")
        with open(extracted_path, "wb") as out:
            out.write(executable)
        return loadseg.load(extracted_path, patched=False)[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", help="original .adf or extracted Amiga Hunk executable")
    args = parser.parse_args()
    try:
        image = relocated_image(args.executable)
        references = find_references(image)
    except (OSError, ValueError, IndexError, struct.error) as error:
        parser.exit(2, f"cannot inspect executable: {error}\n")

    for ref in references:
        locations = ", ".join(f"${loc:06X}" for loc in ref.locations) or "NOT FOUND"
        print(f"plane {ref.plane} {ref.mode:11} "
              f"btst.b #{ref.plane},${ref.operand:06X}: {locations}")

    if matches_reference(references):
        print("MATCH: all eight BTST instructions use the reconstructed "
              "plane flag bytes in the expected order")
        return 0
    print("INCONCLUSIVE: original executable differs from the independent "
          "reconstruction; do not change the native line observer yet")
    for pc, bit, address in find_other_selectors(image):
        print(f"  other selector at ${pc:06X}: btst.b #{bit},${address:06X}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
