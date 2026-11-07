#!/usr/bin/env python3
"""Bound pix/inst5 structural-panel changes to ADF-backed static cells."""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))

from adf import Adf
from extract_cockpit import decode_ilbm
from compare_ppm_center import read_ppm


def structural(x: int, y: int) -> bool:
    if not (0 <= x < 320 and 145 <= y < 200):
        return False
    return y == 145 or y == 199 or x < 4 or 125 <= x < 133


def main() -> None:
    if len(sys.argv) not in (2, 4):
        sys.exit("usage: check_gl_cockpit_panel.py ORIGINAL.adf [reference.ppm native.ppm]")
    disk = Adf(sys.argv[1])
    files = {path: sector for path, sector, size in disk.walk() if size is not None}
    iw, ih, rgba = decode_ilbm(disk.read_file(files["pix/inst5"]))
    assert (iw, ih) == (320, 55)
    source_cells = [(x, y) for y in range(145, 200) for x in range(320)
                    if structural(x, y)]
    opaque = sum(rgba[((y - 145) * iw + x) * 4 + 3] != 0
                 for x, y in source_cells)
    black = sum(not any(rgba[((y - 145) * iw + x) * 4:
                                 ((y - 145) * iw + x) * 4 + 3])
                for x, y in source_cells)
    assert len(source_cells) == 1276 and opaque == 1276 and black >= 100, (
        "unexpected inst5 structure / ADF masking; recheck the panel stencil"
    )
    if len(sys.argv) == 2:
        print(f"PASS: {opaque} ADF-backed structural cells ({black} opaque black)")
        return
    width, height, original = read_ppm(sys.argv[2])
    nw, nh, native = read_ppm(sys.argv[3])
    assert (width, height) == (nw, nh) and height == 600 and width > 960
    scale = 3
    left = (width - 320 * scale) // 2
    changed = 0
    # Compare the entire output, not just the intended lower-panel window:
    # the toggle must not change any flight, sideband or HUD pixels.
    for y in range(height):
        for x in range(width):
            k = (y * width + x) * 3
            if original[k:k + 3] == native[k:k + 3]:
                continue
            sx = (x - left) // scale
            sy = y // scale
            assert structural(sx, sy), (
                f"native panel changed a dynamic/non-panel pixel at {x},{y}"
            )
            pixel = (sy - 145) * iw + sx
            assert rgba[pixel * 4 + 3], (
                f"native panel changed transparent ADF cell at {x},{y}"
            )
            changed += 1

    print(f"PASS: {changed} differences confined to inst5 static structure")


if __name__ == "__main__":
    main()
