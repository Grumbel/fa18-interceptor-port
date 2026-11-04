#!/usr/bin/env python3
"""Check native HUD crosshair replaces only its 5x5 bitmap footprint."""

import sys

from compare_ppm_center import read_ppm


def main():
    width, height, native = read_ppm(sys.argv[1])
    old_width, old_height, original = read_ppm(sys.argv[2])
    assert (width, height) == (old_width, old_height)
    assert width > 960 and height == 600, "expected 3x widescreen screenshots"

    changed = []
    for y in range(height):
        for x in range(width):
            at = (y * width + x) * 3
            if native[at:at + 3] != original[at:at + 3]:
                changed.append((x, y))
    assert 25 <= len(changed) <= 110, f"unexpected native reticle pixels: {len(changed)}"
    for x, y in changed:
        assert abs(x - width // 2) <= 14 and 208 <= y <= 232, (x, y)

    # A source pixel is three pixels wide in the legacy screenshot. Native
    # strokes introduce fractional-coverage shades in the same footprint.
    old_colours = {
        original[(y * width + x) * 3:(y * width + x) * 3 + 3]
        for x in range(width // 2 - 14, width // 2 + 15)
        for y in range(208, 233)
    }
    antialias = {
        native[(y * width + x) * 3:(y * width + x) * 3 + 3]
        for x, y in changed
    } - old_colours
    assert len(antialias) >= 2, "native crosshair has no antialiased edge shades"
    print(f"PASS: {len(changed)} source-reticle pixels superseded by native antialiased strokes")


if __name__ == "__main__":
    main()
