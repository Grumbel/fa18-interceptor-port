#!/usr/bin/env python3
"""Bound differences from native frnt5 artwork under the HUD rails."""

import sys

from compare_ppm_center import read_ppm


def main() -> None:
    width, height, old = read_ppm(sys.argv[1])
    new_width, new_height, native = read_ppm(sys.argv[2])
    assert (width, height) == (new_width, new_height)
    assert width > 960 and height == 600, "expected 3x widescreen screenshot"
    scale = 3
    left = (width - 320 * scale) // 2

    changed = 0
    for y in range(height):
        for x in range(width):
            pos = (y * width + x) * 3
            if old[pos:pos + 3] == native[pos:pos + 3]:
                continue
            source_x = (x - left) // scale
            source_y = y // scale
            assert left <= x < left + 320 * scale, (x, y)
            assert source_x in (85, 233) and 138 <= source_y <= 144, (
                f"unexpected change outside the two cockpit-rail footprints: {x},{y}"
            )
            changed += 1
    # The previous two-neighbour average can happen to equal the underlying
    # ILBM colour. The log-side asset count proves writer-backed use even then.
    print(f"PASS: {changed} pixels changed only at the two front-lip rail cells")


if __name__ == "__main__":
    main()
