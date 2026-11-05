#!/usr/bin/env python3
"""Check that native Pause strokes use the original copper pen and appear on time."""

from collections import Counter
from pathlib import Path
import sys


def ppm(path: Path):
    with path.open("rb") as stream:
        assert stream.readline() == b"P6\n", path
        width, height = map(int, stream.readline().split())
        assert stream.readline() == b"255\n", path
        pixels = stream.read()
    assert len(pixels) == width * height * 3, path
    return width, height, pixels


def region(image, x0, y0, x1, y1):
    width, height, pixels = image
    assert 0 <= x0 < x1 <= width and 0 <= y0 < y1 <= height
    for y in range(y0, y1):
        for x in range(x0, x1):
            pos = 3 * (y * width + x)
            yield tuple(pixels[pos:pos + 3])


def amber(pixel):
    r, g, b = pixel
    return r >= 32 and r > g > b and g > b + 12


def main(original, early, native):
    assert original[:2] == (320, 200)
    colours = Counter(p for p in region(original, 124, 42, 202, 51) if amber(p))
    assert colours, "original Pause heading was not drawn"
    ink, samples = colours.most_common(1)[0]
    assert samples >= 20, f"insufficient original pen samples: {samples}"

    # The widescreen screenshot has a 960-pixel-wide 3x native centre and
    # rescales only vertically for the 320x200 Amiga source (600 pixels).
    expected_width = 960
    assert native[0] >= expected_width and native[1] == 600
    centre = (native[0] - expected_width) // 2
    box = (centre + 124 * 3, 42 * 3, centre + 202 * 3, 51 * 3)
    bright = Counter(p for p in region(native, *box) if amber(p))
    assert bright[ink] >= 20, (
        f"native title did not use source copper ink {ink}: {bright.most_common(5)}"
    )
    assert all(p[0] <= ink[0] and p[1] <= ink[1] for p in bright), (
        f"native heading is brighter than live copper pen {ink}: {bright.most_common(5)}"
    )

    assert early[:2] == native[:2]
    before = Counter(p for p in region(early, *box) if amber(p))
    assert not before, f"native heading appeared before original glyphs: {before.most_common(4)}"
    print(f"PASS: native Pause heading follows original copper ink {ink} "
          f"({bright[ink]} opaque pixels) and waits for the source title")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: check_gl_pause_ink.py original.ppm early.ppm native.ppm")
    main(*(ppm(Path(arg)) for arg in sys.argv[1:]))
