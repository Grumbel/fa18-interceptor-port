#!/usr/bin/env python3
"""Check the experimental native globe's source-asset colour and bounds.

Usage: python3 tests/check_globe_asset.py ORIGINAL.adf
No original art is stored in the repository.
"""

import collections
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from adf import Adf
from extract_cockpit import decode_byterun1
from extract_cockpit import decode_ilbm
from preview_globe import render as render_preview


def indexed_ilbm(data: bytes) -> tuple[int, int, bytes, bytes]:
    if data[:4] != b"FORM" or data[8:12] != b"ILBM":
        raise ValueError("not an ILBM")
    limit = min(len(data), 8 + struct.unpack_from(">I", data, 4)[0])
    chunks = {}
    off = 12
    while off + 8 <= limit:
        kind = data[off:off + 4]
        size = struct.unpack_from(">I", data, off + 4)[0]
        off += 8
        if off + size > limit:
            raise ValueError("invalid ILBM chunk")
        chunks[kind] = data[off:off + size]
        off += size + (size & 1)
    bmhd = chunks[b"BMHD"]
    width, height = struct.unpack_from(">HH", bmhd)
    assert (width, height, bmhd[8], bmhd[10]) == (320, 55, 5, 1)
    row_bytes = ((width + 15) // 16) * 2
    pixels = decode_byterun1(chunks[b"BODY"], row_bytes * height * 5)
    indices = bytearray(width * height)
    for y in range(height):
        for x in range(width):
            value = 0
            for plane in range(5):
                index = (y * 5 + plane) * row_bytes + x // 8
                value |= ((pixels[index] >> (7 - (x & 7))) & 1) << plane
            indices[y * width + x] = value
    return width, height, bytes(indices), chunks[b"CMAP"]


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit("usage: tests/check_globe_asset.py ORIGINAL.adf")
    disk = Adf(sys.argv[1])
    entries = {path: sector for path, sector, size in disk.walk() if size is not None}
    width, height, image, cmap = indexed_ilbm(disk.read_file(entries["pix/inst5"]))
    assert (width, height) == (320, 55)
    # The level globe is two filled pen regions, not an opaque sprite.
    # Other indices (outline/ticks/lettering) must remain in the source layer.
    assert cmap[17 * 3:17 * 3 + 3] == bytes([128, 0, 0])
    assert cmap[21 * 3:21 * 3 + 3] == bytes([64, 64, 112])
    counts = collections.Counter()
    points = []
    for y in range(146, 194):
        for x in range(189, 230):
            pen = image[(y - 145) * width + x]
            if pen not in (17, 21):
                continue
            counts[pen] += 1
            points.append((x, y))
            assert 198 <= x < 221 and 163 <= y < 184
            # In the *original level asset*, every sky pixel is strictly
            # above y=173; this anchors the 0-bank/0-pitch model.
            assert (y < 173) == (pen == 21)
    assert counts == {17: 186, 21: 172}
    bounds = (min(x for x, _ in points), min(y for _, y in points),
              max(x for x, _ in points), max(y for _, y in points))
    assert bounds == (198, 163, 220, 183)
    # At the level pose the offline preview must be pixel-identical to the
    # asset. A nonzero bank may change only its red/blue fill cells.
    dw, dh, rgba = decode_ilbm(disk.read_file(entries["pix/inst5"]))
    assert (dw, dh) == (width, height)
    assert render_preview(width, height, rgba, 0, 0, 1) == rgba
    rotated = render_preview(width, height, rgba, 30, 2, 1)
    assert rotated != rgba
    for y in range(height):
        for x in range(width):
            at = (y * width + x) * 4
            if rotated[at:at + 4] == rgba[at:at + 4]:
                continue
            assert 198 <= x < 221 and 163 <= y + 145 < 184
            assert image[y * width + x] in (17, 21)
    print(f"PASS: globe red={counts[17]} blue={counts[21]} bbox={bounds}")


if __name__ == "__main__":
    main()
