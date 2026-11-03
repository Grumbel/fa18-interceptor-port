#!/usr/bin/env python3
"""Assert that the middle of a wide P6 screenshot is pixel-exactly 4:3."""
import sys


def read_ppm(path):
    with open(path, "rb") as f:
        assert f.readline() == b"P6\n", f"not a P6 PPM: {path}"
        w, h = map(int, f.readline().split())
        assert f.readline() == b"255\n", f"unexpected PPM max value: {path}"
        data = f.read()
        assert len(data) == w * h * 3, f"incorrect PPM size: {path}"
    return w, h, data


def main():
    narrow_w, narrow_h, narrow = read_ppm(sys.argv[1])
    wide_w, wide_h, wide = read_ppm(sys.argv[2])
    assert narrow_h == wide_h and wide_w > narrow_w
    assert (wide_w - narrow_w) % 2 == 0
    offset = (wide_w - narrow_w) // 2
    assert any(wide[y * wide_w * 3:y * wide_w * 3 + offset * 3]
               != b"\x00" * (offset * 3) for y in range(wide_h)), \
        "wide extension contains no rendered scenery"
    for y in range(narrow_h):
        a = narrow[y * narrow_w * 3:(y + 1) * narrow_w * 3]
        b = wide[(y * wide_w + offset) * 3:(y * wide_w + offset + narrow_w) * 3]
        assert a == b, f"widescreen changed the original view at row {y}"
    print(f"PASS: widescreen {wide_w}x{wide_h} adds scenery and preserves "
          f"all {narrow_w}x{narrow_h} center pixels")


if __name__ == "__main__":
    main()
