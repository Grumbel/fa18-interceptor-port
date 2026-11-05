#!/usr/bin/env python3
"""Ensure high-resolution fuel digits only replace their original LCD cells."""

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
    xmin = side + 104 * scale
    xmax = side + 123 * scale
    ymin = 183 * scale
    ymax = 188 * scale
    inside = outside = 0
    for y in range(h):
        for x in range(w):
            at = (y * w + x) * 3
            if old[at:at + 3] == new[at:at + 3]:
                continue
            if xmin <= x < xmax and ymin <= y < ymax:
                inside += 1
            else:
                outside += 1
                if outside < 5:
                    print(f"Unexpected fuel vector change at {x},{y}", file=sys.stderr)
    assert outside == 0, f"vector drawing changed {outside} pixels outside the LCD"
    assert allow_unchanged or inside >= 10, (
        f"no native fuel strokes rendered ({inside} changed pixels)"
    )
    if inside:
        print(f"PASS: native LCD changes {inside} bounded high-res pixels, none outside")
    else:
        print("PASS: unchanged fuel LCD conservatively retains its original pixels")


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        raise SystemExit("usage: check_gl_fuel_vector.py reference.ppm vector.ppm [--allow-unchanged]")
    assert len(sys.argv) == 3 or sys.argv[3] == "--allow-unchanged"
    compare(Path(sys.argv[1]), Path(sys.argv[2]), len(sys.argv) == 4)
