#!/usr/bin/env python3
"""Bound native front-nose changes to the ADF's actually painted ILBM cells."""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))

from adf import Adf
from extract_cockpit import decode_ilbm
from compare_ppm_center import read_ppm


def main() -> None:
    disk = Adf(sys.argv[1])
    files = {path: sector for path, sector, size in disk.walk() if size is not None}
    iw, ih, rgba = decode_ilbm(disk.read_file(files["pix/frnt5"]))
    assert (iw, ih) == (288, 12)

    width, height, original = read_ppm(sys.argv[2])
    nw, nh, native = read_ppm(sys.argv[3])
    assert (width, height) == (nw, nh) and height == 600 and width > 960
    scale = 3
    left = (width - 320 * scale) // 2
    changed = 0

    for y in range(133 * scale, 145 * scale):
        for x in range(left + 16 * scale, left + 304 * scale):
            k = (y * width + x) * 3
            if original[k:k + 3] == native[k:k + 3]:
                continue
            sx = (x - left) // scale - 16
            sy = y // scale - 133
            p = (sy * iw + sx) * 4
            assert rgba[p + 3] and any(rgba[p:p + 3]), (
                f"native change outside painted front-nose ILBM: {x},{y}"
            )
            changed += 1

    # Also reject unintended changes *outside* the front nose entirely.
    # Span checks above establish the exact ADF mask; bytearray comparison
    # elsewhere catches changes from unrelated presentation layers.
    for y in range(height):
        row = y * width * 3
        if not (133 * scale <= y < 145 * scale):
            assert original[row:row + width * 3] == native[row:row + width * 3], (
                f"native front-nose toggle changed row {y}"
            )
            continue
        for x in list(range(left + 16 * scale)) + list(range(left + 304 * scale, width)):
            k = row + x * 3
            assert original[k:k + 3] == native[k:k + 3], (
                f"native front-nose toggle changed outside asset: {x},{y}"
            )

    print(f"PASS: {changed} changed pixels lie exclusively on original frnt5 nose ink")


if __name__ == "__main__":
    main()
