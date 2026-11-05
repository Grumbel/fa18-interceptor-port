#!/usr/bin/env python3
"""Bounds-check the native gauge and its 23-row original fuel scaling."""

from pathlib import Path
import sys


def ppm(path: str):
    with Path(path).open("rb") as stream:
        assert stream.readline() == b"P6\n", path
        width, height = map(int, stream.readline().split())
        assert stream.readline() == b"255\n", path
        pixels = stream.read()
    assert len(pixels) == width * height * 3, path
    return width, height, pixels


def original_height(path: str, expected: int):
    width, height, pixels = ppm(path)
    assert (width, height) == (320, 200)

    def at(x, y):
        offset = (y * width + x) * 3
        return pixels[offset:offset + 3]

    green = at(112, 180)
    empty = at(110, 158)
    assert green != empty, "original bar has no filled colour"
    for y in range(158, 181):
        present = y >= 181 - expected
        for x in range(111, 115):
            assert at(x, y) == (green if present else empty), (
                f"original fill at ({x}, {y}) disagrees with {expected} rows"
            )
    print(f"PASS: source fuel bar is exactly {expected} of 23 rows")


def bounded_change(old_path: str, new_path: str):
    width, height, old = ppm(old_path)
    new_w, new_h, new = ppm(new_path)
    assert (width, height) == (new_w, new_h) and height == 600
    scale = height // 200
    side = (width - 320 * scale) // 2
    assert side >= 0
    bounds = (side + 111 * scale, side + 115 * scale,
              158 * scale, 181 * scale)
    inside = outside = 0
    for y in range(height):
        for x in range(width):
            offset = (y * width + x) * 3
            if old[offset:offset + 3] == new[offset:offset + 3]:
                continue
            if bounds[0] <= x < bounds[1] and bounds[2] <= y < bounds[3]:
                inside += 1
            else:
                outside += 1
                if outside <= 3:
                    print(f"unexpected gauge change at {x},{y}", file=sys.stderr)
    assert outside == 0, f"gauge modified {outside} pixels outside the bar"
    assert inside >= 10, f"native gauge not visible ({inside} pixels)"
    print(f"PASS: {inside} native pixels differ inside the bar, none outside")


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--original":
        original_height(sys.argv[2], int(sys.argv[3]))
    elif len(sys.argv) == 3:
        bounded_change(sys.argv[1], sys.argv[2])
    else:
        raise SystemExit("usage: check_gl_fuel_bar.py [--original source.ppm rows] | old.ppm new.ppm")
