#!/usr/bin/env python3
"""Original-game radar writers and bounded CPU native-vector replacement."""

from pathlib import Path
import re
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


def writer_capture(log_path, screenshot_frame, expected):
    """The original pen-5 writer must see the contact in BOTH screen banks."""
    records = {}
    for line in Path(log_path).read_text().splitlines():
        match = re.search(r"^radar writer frame=(\d+) bank=([0-9a-f]+) "
                          r"x=(\d+) y=(\d+) mask=([0-9a-f]+) a3=([0-9a-f]+)", line)
        if not match:
            continue
        frame, bank, x, y, mask, destination = match.groups()
        frame, x, y, mask, destination = (int(frame), int(x), int(y),
                                          int(mask, 16), int(destination, 16))
        if frame > screenshot_frame:
            continue
        assert mask.bit_count() == 1, (frame, mask)
        assert mask == 1 << (15 - x % 16), (frame, x, mask)
        assert int(bank, 16) == destination - y * 40 - (x // 16) * 2 + 183 * 40, bank
        records[bank] = (frame, x, y)
    assert len(records) == 2, f"expected two observed radar banks: {records}"
    for bank, (frame, x, y) in records.items():
        assert (x, y) == expected, (bank, (x, y), expected)
        assert screenshot_frame - frame <= 24, (bank, frame, screenshot_frame)
    print(f"PASS: pen-5 radar writer captured {expected} in both buffers "
          f"before frame {screenshot_frame}")


def stencil_capture(log_path, screenshot_frame):
    """Check original 16-pixel blits and four pen-12 points in BOTH buffers.

    The game refreshes the line stencil less often than the moving dot; a
    decoded blit remains current until the game redraws that screen bank.
    """
    guides, markers = {}, {}
    for line in Path(log_path).read_text().splitlines():
        guide = re.search(r"^radar guide frame=(\d+) bank=([0-9a-f]+) "
                          r"x=(\d+) y=(\d+) len=(\d+) slope=([0-9a-f]+) "
                          r"complete=(\d+)", line)
        if guide:
            frame, bank, x, y, length, slope, done = guide.groups()
            frame, x, y, length, slope, done = (int(frame), int(x), int(y),
                                                  int(length), int(slope, 16),
                                                  int(done))
            assert (x, length, slope) in ((139, 16, 0x11), (177, 16, 0x15))
            assert y in (147, 148, 149), (frame, y)
            if frame <= screenshot_frame:
                previous = guides.get(bank, (None, None, set()))
                if previous[0] != frame:
                    previous = (frame, y, set())
                assert previous[1] == y, (bank, frame, previous, y)
                previous[2].add(x)
                guides[bank] = previous
                if done:
                    assert previous[2] == {139, 177}, (bank, frame, previous)
            continue
        marker = re.search(r"^radar marker frame=(\d+) bank=([0-9a-f]+) "
                           r"x=(\d+) y=(\d+) complete=(\d+)", line)
        if marker:
            frame, bank, x, y, done = marker.groups()
            frame, x, y, done = int(frame), int(x), int(y), int(done)
            assert done == 1, (bank, frame)
            assert x == 158 and y in (166, 167, 168), (bank, frame, x, y)
            if frame <= screenshot_frame:
                markers[bank] = (frame, x, y)

    assert len(guides) == len(markers) == 2, (guides, markers)
    assert guides.keys() == markers.keys(), (guides, markers)
    for bank, (frame, y, x_values) in guides.items():
        assert x_values == {139, 177}, (bank, frame, x_values)
        assert screenshot_frame - frame <= 128, (bank, frame, screenshot_frame)
        marker_frame, _, _ = markers[bank]
        assert screenshot_frame - marker_frame <= 128, (bank, marker_frame)
    print(f"PASS: both radar banks captured complete blitter guides and pen-12 "
          f"marker before frame {screenshot_frame}")


if __name__ == "__main__":
    if len(sys.argv) == 5 and sys.argv[1] == "--original":
        original(sys.argv[2], (int(sys.argv[3]), int(sys.argv[4])))
    elif len(sys.argv) == 6 and sys.argv[1] == "--writer":
        writer_capture(sys.argv[2], int(sys.argv[3]),
                       (int(sys.argv[4]), int(sys.argv[5])))
    elif len(sys.argv) == 4 and sys.argv[1] == "--stencil":
        stencil_capture(sys.argv[2], int(sys.argv[3]))
    elif len(sys.argv) == 3:
        bounds(sys.argv[1], sys.argv[2])
    else:
        sys.exit("usage: check_gl_radar_vector.py --original 1x.ppm x y "
                 "| --writer writes.log frame x y | --stencil writes.log frame "
                 "| old.ppm new.ppm")
