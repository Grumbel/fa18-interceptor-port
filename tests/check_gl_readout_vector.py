#!/usr/bin/env python3
"""Bound native 3x5 readout strokes to their measured cockpit cells."""

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


def compare(reference: Path, vector: Path, allow_unchanged: bool = False):
    old_w, old_h, old = ppm(reference)
    w, h, new = ppm(vector)
    assert (w, h) == (old_w, old_h) and h == 600
    scale = h // 200
    side = (w - 320 * scale) // 2
    assert side >= 0
    # These source rectangles are the actual 68000 LCD font-writer targets.
    regions = ((144, 155, 184, 189), (240, 266, 158, 189),
               (292, 308, 180, 193))
    inside = outside = 0
    for y in range(h):
        for x in range(w):
            at = (y * w + x) * 3
            if old[at:at + 3] == new[at:at + 3]:
                continue
            sx = (x - side) // scale
            sy = y // scale
            if any(x0 <= sx < x1 and y0 <= sy < y1
                   for x0, x1, y0, y1 in regions):
                inside += 1
            else:
                outside += 1
                if outside < 5:
                    print(f"Unexpected readout stroke change at {x},{y}",
                          file=sys.stderr)
    assert outside == 0, f"readout vector changed {outside} pixels outside the LCDs"
    assert allow_unchanged or inside >= 10, (
        f"no native cockpit readout strokes rendered ({inside} changed pixels)"
    )
    print(f"PASS: {inside} readout stroke pixels, all within original LCD cells")


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        raise SystemExit("usage: check_gl_readout_vector.py reference.ppm vector.ppm "
                         "[--allow-unchanged]")
    assert len(sys.argv) == 3 or sys.argv[3] == "--allow-unchanged"
    compare(Path(sys.argv[1]), Path(sys.argv[2]), len(sys.argv) == 4)
