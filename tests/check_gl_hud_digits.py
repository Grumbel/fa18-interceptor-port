#!/usr/bin/env python3
"""Bound the vector flight numerals to their original screen-space cells."""

import sys

from compare_ppm_center import read_ppm


def main():
    width, height, native = read_ppm(sys.argv[1])
    old_width, old_height, legacy = read_ppm(sys.argv[2])
    assert (width, height) == (old_width, old_height)
    assert width > 960 and height == 600, "expected 3x widescreen output"
    left = (width - 960) // 2
    boxes = (
        (139, 169, 52, 59),  # two pairs of flight HUD numbers
        (97, 111, 83, 91),   # airspeed
        (217, 231, 83, 91), # altitude
        (95, 107, 120, 128), # G readout
    )
    changed = [0] * len(boxes)
    pixels = 0
    gradients = set()
    for y in range(height):
        for x in range(width):
            offset = (y * width + x) * 3
            ink = native[offset:offset + 3]
            if ink == legacy[offset:offset + 3]:
                continue
            pixels += 1
            sx = (x - left) / 3
            sy = y / 3
            matches = [i for i, (x0, x1, y0, y1) in enumerate(boxes)
                       if x0 <= sx < x1 and y0 <= sy < y1]
            assert matches, f"numeral changed outside HUD text: {(x, y)}"
            for i in matches:
                changed[i] += 1
            if matches[0] == 0:
                gradients.add(ink)

    if sys.argv[3] == "flight":
        assert pixels >= 250, f"native glyphs missing: {pixels} changed pixels"
        assert changed[0] >= 100, "top digits not reconstructed"
        assert changed[1] >= 30 and changed[2] >= 30, "speed/altitude missing"
        assert changed[3] >= 30, "G readout missing"
        assert len(gradients) >= 3, "no native antialias shades"
    print(f"PASS: {sys.argv[3]} native HUD numbers: {pixels} bounded pixels, "
          f"fields={changed}")


if __name__ == "__main__":
    main()
