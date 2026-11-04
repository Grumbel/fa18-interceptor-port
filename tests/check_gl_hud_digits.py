#!/usr/bin/env python3
"""Bound vector flight numerals and unit labels to their source cells."""

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
        (97, 112, 83, 98),   # airspeed and KT label
        (217, 232, 83, 98), # altitude and FT label
        (95, 113, 120, 129), # G readout and G label
    )
    labels = (
        (101, 106, 90, 97), # K
        (105, 110, 90, 97), # T
        (221, 226, 90, 97), # F
        (225, 230, 90, 97), # T
        (107, 112, 120, 127), # G
    )
    changed = [0] * len(boxes)
    label_changed = [0] * len(labels)
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
            for i, (x0, x1, y0, y1) in enumerate(labels):
                if x0 <= sx < x1 and y0 <= sy < y1:
                    label_changed[i] += 1
            if matches[0] == 0:
                gradients.add(ink)

    if sys.argv[3] == "flight":
        assert pixels >= 250, f"native glyphs missing: {pixels} changed pixels"
        assert changed[0] >= 100, "top digits not reconstructed"
        assert changed[1] >= 30 and changed[2] >= 30, "speed/altitude missing"
        assert changed[3] >= 30, "G readout missing"
        assert all(n >= 10 for n in label_changed), (
            f"native KT/FT/G glyphs missing: {label_changed}")
        assert len(gradients) >= 3, "no native antialias shades"
    print(f"PASS: {sys.argv[3]} native HUD glyphs: {pixels} bounded pixels, "
          f"fields={changed} labels={label_changed}")


if __name__ == "__main__":
    main()
