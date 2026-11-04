#!/usr/bin/env python3
"""The two native HUD rails must replace only the original vertical pixels."""

import sys

from compare_ppm_center import read_ppm


def main():
    width, height, native = read_ppm(sys.argv[1])
    old_width, old_height, original = read_ppm(sys.argv[2])
    assert (width, height) == (old_width, old_height)
    assert width > 960 and height == 600, "expected 3x widescreen screenshots"
    left = (width - 960) // 2

    changed = []
    for y in range(height):
        for x in range(width):
            at = (y * width + x) * 3
            if native[at:at + 3] != original[at:at + 3]:
                changed.append((x, y))
                assert 45 * 3 <= y < 146 * 3, (x, y)
                assert any(left + (rail - 1) * 3 <= x < left + (rail + 2) * 3
                           for rail in (85, 233)), (x, y)

    assert len(changed) >= 150, f"native rails missing: {len(changed)} changed pixels"
    for rail in (85, 233):
        start = left + rail * 3
        assert any(start <= x < start + 3 and 55 * 3 <= y < 85 * 3
                   for x, y in changed), f"rail {rail} did not change"
    assert any(138 * 3 <= y < 145 * 3 for _, y in changed), \
        "native rail did not preserve the transition onto the cockpit art"
    # At the clear-sky height, the centre of each 3x source pixel is black
    # while its edge pixels have fractional background coverage. This
    # distinguishes a native thin stroke from a merely recoloured bitmap.
    sky_y = 60 * 3 + 1
    for rail in (85, 233):
        cx = left + rail * 3 + 1
        def rgb(x):
            at = (sky_y * width + x) * 3
            return native[at:at + 3]
        assert rgb(cx) == bytes(3), f"native rail {rail} has no solid core"
        assert rgb(cx - 1) != bytes(3) and rgb(cx + 1) != bytes(3), \
            f"native rail {rail} has no antialiased edges"
    print(f"PASS: {len(changed)} pixels changed only on native HUD vertical rails")


if __name__ == "__main__":
    main()
