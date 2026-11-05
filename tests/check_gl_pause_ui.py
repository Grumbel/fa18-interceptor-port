#!/usr/bin/env python3
"""Check that GL Pause has true high-resolution letter edges.

The title uses the original *live copper* pen. Its RGB can legitimately be
the same as the 320x200 source ink, so a hardcoded colour-inequality test
would incorrectly reject a perfectly correct antialiased native title.
"""

from collections import Counter
import sys

from compare_ppm_center import read_ppm


def main():
    width, height, data = read_ppm(sys.argv[1])
    assert width > 960 and height == 600, "expected a 3x widescreen Pause capture"
    left = (width - 960) // 2

    def pixel(x, y):
        at = (y * width + x) * 3
        return tuple(data[at:at + 3])

    # A genuine 3x enlargement of the Amiga bitmap is constant within each
    # 3x3 source pixel. The native strokes have mixed/edge-coverage colours
    # *within* these source pixels. Check the title's own source rectangle,
    # independent of colour fades and the wider scene to either side.
    mixed_cells = 0
    ink = Counter()
    for sy in range(42, 51):
        for sx in range(124, 202):
            colours = {
                pixel(left + sx * 3 + xx, sy * 3 + yy)
                for yy in range(3) for xx in range(3)
            }
            mixed_cells += len(colours) > 1
            ink.update(c for c in colours if c[0] >= 32 and c[0] > c[1] > c[2])

    assert ink, "Pause title ink was not drawn"
    assert mixed_cells > 30, (
        f"only {mixed_cells} subpixel-changing source cells; original title may be scaled"
    )
    print(f"PASS: native Pause lettering has {mixed_cells} antialiased source cells "
          f"with live copper-coloured ink {ink.most_common(1)[0][0]}")


if __name__ == "__main__":
    main()
