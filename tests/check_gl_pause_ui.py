#!/usr/bin/env python3
"""Check that GL Pause uses vector-height lettering, not scaled bitmap text."""

import sys

from compare_ppm_center import read_ppm


def main():
    width, height, data = read_ppm(sys.argv[1])
    assert width > 960 and height == 600, "expected a 3x widescreen Pause capture"
    old = native = antialias = 0
    for y in range(126, 153):
        for x in range(width // 2 - 110, width // 2 + 110):
            at = (y * width + x) * 3
            rgb = data[at:at + 3]
            old += rgb == b"\x88\x44\x00"
            native += rgb == b"\xb8\x78\x00"
            antialias += 0 < rgb[0] < 184 and 0 < rgb[1] < 120 and rgb[2] > 0
    assert not old, f"{old} original low-resolution Pause title pixels survived"
    assert native > 200, f"only {native} native vector title pixels"
    assert antialias > 40, f"only {antialias} antialiased edge pixels"
    print("PASS: low-resolution Pause lettering replaced by antialiased native UI")


if __name__ == "__main__":
    main()
