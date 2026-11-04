#!/usr/bin/env python3
"""Bound native heading-tape changes to the five ticks and centre index."""

import sys

from compare_ppm_center import read_ppm


def main():
    width, height, native = read_ppm(sys.argv[1])
    old_width, old_height, original = read_ppm(sys.argv[2])
    assert (width, height) == (old_width, old_height)
    assert width > 960 and height == 600, "expected 3x widescreen frame"
    left = (width - 960) // 2
    changes = [0, 0]
    shades = set()
    for y in range(height):
        for x in range(width):
            off = 3 * (y * width + x)
            if native[off:off + 3] == original[off:off + 3]:
                continue
            sx = (x - left) / 3
            sy = y / 3
            if 136 <= sx < 192 and 59 <= sy < 63:
                changes[0] += 1
            elif 157 <= sx < 162 and 62 <= sy < 67:
                changes[1] += 1
            else:
                raise AssertionError(f"heading changed outside tick/index region: {(x, y)}")
            shades.add(native[off:off + 3])
    assert changes[0] >= 40, f"native heading ticks absent: {changes}"
    assert changes[1] >= 10, f"native centre index absent: {changes}"
    assert len(shades) >= 3, "heading strokes lack antialiasing"
    print(f"PASS: {sys.argv[3]} heading ticks/index bounded, changed={changes}")


if __name__ == "__main__":
    main()
