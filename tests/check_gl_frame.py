#!/usr/bin/env python3
"""Verify the native HUD crown supersedes only its bitmap stroke footprint."""

import sys

from compare_ppm_center import read_ppm


def main():
    width, height, native = read_ppm(sys.argv[1])
    old_width, old_height, legacy = read_ppm(sys.argv[2])
    assert (width, height) == (old_width, old_height)
    assert width > 960 and height == 600, "expected 3x widescreen output"
    left = (width - 960) // 2

    changed = []
    for y in range(height):
        for x in range(width):
            at = (y * width + x) * 3
            if native[at:at + 3] != legacy[at:at + 3]:
                changed.append((x, y))
                assert left + 83 * 3 <= x < left + 236 * 3, (x, y)
                assert 35 * 3 <= y < 48 * 3, (x, y)

    assert len(changed) >= 200, f"HUD frame crown was not drawn: {len(changed)}"
    assert any(37 * 3 <= y < 38 * 3 for _, y in changed), "top rail unchanged"
    assert any(41 * 3 <= y < 45 * 3 for _, y in changed), "chamfers unchanged"

    # Anti-aliasing must add gradients beyond the two source pen colours.
    old_ink = {
        legacy[(y * width + x) * 3:(y * width + x) * 3 + 3]
        for x in range(left + 90 * 3, left + 100 * 3)
        for y in range(36 * 3, 40 * 3)
    }
    new_ink = {
        native[(y * width + x) * 3:(y * width + x) * 3 + 3]
        for x, y in changed
        if left + 90 * 3 <= x < left + 100 * 3 and y < 40 * 3
    }
    assert len(new_ink - old_ink) >= 2, "no antialiased crown edge shades"
    print(f"PASS: {len(changed)} pixels in native top rail/chamfers, none outside")


if __name__ == "__main__":
    main()
