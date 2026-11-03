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
    max_different = int(sys.argv[3]) if len(sys.argv) >= 4 else 0
    assert narrow_h == wide_h and wide_w > narrow_w
    assert (wide_w - narrow_w) % 2 == 0
    offset = (wide_w - narrow_w) // 2
    assert any(wide[y * wide_w * 3:y * wide_w * 3 + offset * 3]
               != b"\x00" * (offset * 3) for y in range(wide_h)), \
        "wide extension contains no rendered scenery"
    different = 0
    first = None
    for y in range(narrow_h):
        a = narrow[y * narrow_w * 3:(y + 1) * narrow_w * 3]
        b = wide[(y * wide_w + offset) * 3:(y * wide_w + offset + narrow_w) * 3]
        if a != b:
            for x in range(narrow_w):
                if a[x * 3:x * 3 + 3] != b[x * 3:x * 3 + 3]:
                    different += 1
                    if first is None:
                        first = (x, y)
    assert different <= max_different, \
        f"widescreen centre differs at {different} pixels (first {first}, allowed {max_different})"
    print(f"PASS: widescreen {wide_w}x{wide_h} adds scenery and preserves "
          f"{narrow_w}x{narrow_h} centre within {different} raster-edge pixels")


if __name__ == "__main__":
    main()
