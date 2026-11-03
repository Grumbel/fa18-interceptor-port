#!/usr/bin/env python3
"""Regression for a transition that used to flash black across 3D scanlines.

The scripted launch displays an older completed GL scene while the Amiga
display is temporarily pointing at a new, incomplete bitplane buffer. The
original overlay must not substitute that black buffer for the centre view,
and the recently rendered scene must remain visible in the side extensions.
"""

import sys

from compare_ppm_center import read_ppm


def pixel(data, width, x, y):
    at = (y * width + x) * 3
    return data[at:at + 3]


def main():
    a_w, a_h, a = read_ppm(sys.argv[1])
    b_w, b_h, b = read_ppm(sys.argv[2])
    assert (a_w, a_h) == (b_w, b_h) and a_w > 320
    side = (a_w - 320) // 2
    assert a_w == 320 + 2 * side

    for frame in (a, b):
        for y in range(6):
            sky = pixel(frame, a_w, side // 2, y)
            assert sky != b"\x00\x00\x00", "widescreen scene blinked black"
            for x in (side, side + 20, side + 160, side + 319):
                assert pixel(frame, a_w, x, y) == sky, (
                    f"incomplete scene buffer flashed at {x},{y}"
                )
            assert pixel(frame, a_w, a_w - side // 2, y) == sky

    # The replay *does* advance; avoid blessing a stale frame accidentally.
    assert a != b, "transition replays did not advance"
    print("PASS: transition preserves matching sky/3D across both widescreen seams")


if __name__ == "__main__":
    main()
