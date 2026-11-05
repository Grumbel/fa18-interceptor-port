#!/usr/bin/env python3
"""Original-game radar stencil and bounded CPU native-vector replacement."""

from pathlib import Path
import sys


def ppm(path):
    with Path(path).open("rb") as stream:
        assert stream.readline() == b"P6\n", path
        width, height = map(int, stream.readline().split())
        assert stream.readline() == b"255\n", path
        pixels = stream.read()
    assert len(pixels) == width * height * 3
    return width, height, pixels


def original(path, expected):
    width, height, pixels = ppm(path)
    assert (width, height) == (320, 200)

    def at(x, y):
        start = (y * width + x) * 3
        return pixels[start:start + 3]

    assert at(139, 148) == bytes((17, 17, 17))
    assert at(158, 167) == bytes((51, 51, 51))
    guides = sum(at(139 + y - 148, y) == bytes((17, 17, 17)) for y in range(148, 164))
    guides += sum(at(177 - y + 148, y) == bytes((17, 17, 17)) for y in range(148, 164))
    assert guides in (31, 32), guides
    for x, y in [(158, 167), (157, 168), (158, 168), (159, 168)]:
        assert at(x, y) == bytes((51, 51, 51)), (x, y)
    dots = [(x, y) for y in range(146, 182) for x in range(135, 182)
            if at(x, y) == bytes((68, 68, 119))]
    assert dots == [expected], f"moving radar dot: {dots} != {[expected]}"
    print(f"PASS: 1x source radar has {guides} guides, four marker pixels, dot {dots[0]}")


def bounds(old_path, new_path):
    width, height, old = ppm(old_path)
    new_width, new_height, new = ppm(new_path)
    assert (width, height) == (new_width, new_height) and height == 600
    scale = height // 200
    side = (width - 320 * scale) // 2
    assert side >= 0
    x0, x1 = side + 135 * scale, side + 182 * scale
    y0, y1 = 146 * scale, 182 * scale
    inside = outside = 0
    for y in range(height):
        for x in range(width):
            offset = (y * width + x) * 3
            if old[offset:offset + 3] == new[offset:offset + 3]:
                continue
            if x0 <= x < x1 and y0 <= y < y1:
                inside += 1
            else:
                outside += 1
                if outside < 4:
                    print(f"Unexpected radar modification at {x},{y}", file=sys.stderr)
    assert outside == 0, f"native radar touched {outside} pixels outside the panel"
    assert inside >= 10, f"native radar had only {inside} changed pixels"
    print(f"PASS: {inside} vector pixels changed inside radar, zero outside")


if __name__ == "__main__":
    if len(sys.argv) == 5 and sys.argv[1] == "--original":
        original(sys.argv[2], (int(sys.argv[3]), int(sys.argv[4])))
    elif len(sys.argv) == 3:
        bounds(sys.argv[1], sys.argv[2])
    else:
        sys.exit("usage: check_gl_radar_vector.py --original 1x.ppm x y | old.ppm new.ppm")
