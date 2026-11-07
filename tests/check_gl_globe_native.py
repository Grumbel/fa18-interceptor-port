#!/usr/bin/env python3
"""Check native globe compositor against original ADF fill and masked scanout."""

from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from adf import Adf
from check_globe_asset import indexed_ilbm


def load_ppm(path: str) -> bytes:
    content = Path(path).read_bytes()
    header = re.match(rb"P6\n640 400\n255\n", content)
    assert header, f"not a 640x400 RGB PPM: {path}"
    pixels = content[header.end():]
    assert len(pixels) == 640 * 400 * 3, path
    return pixels


def rgb(pixels: bytes, x: int, y: int) -> bytes:
    at = (y * 640 + x) * 3
    return pixels[at:at + 3]


def main() -> None:
    if len(sys.argv) != 5:
        sys.exit("usage: check_gl_globe_native.py ORIGINAL.adf reference.ppm native.ppm hidden.ppm")
    disk = Adf(sys.argv[1])
    entries = {p: h for p, h, size in disk.walk() if size is not None}
    width, height, atlas, _ = indexed_ilbm(disk.read_file(entries["pix/inst5"]))
    assert (width, height) == (320, 55)
    reference, native, hidden = map(load_ppm, sys.argv[2:])

    fill = set()
    for y in range(163, 184):
        for x in range(198, 221):
            if atlas[(y - 145) * width + x] in (17, 21):
                fill.add((x, y))
    assert len(fill) == 358

    # The normal opt-in compositor should differ only on the original
    # silhouette and in no more than two source-pixel decisions here.
    changes = [(x, y) for y in range(400) for x in range(640)
               if rgb(reference, x, y) != rgb(native, x, y)]
    assert len(changes) <= 8, ("unmasked native discrepancy", len(changes), changes[:12])
    assert all((x // 2, y // 2) in fill for x, y in changes), changes[:12]

    # Hiding the old scanout produces black holes in the bezel and marks,
    # but the 358 coloured cells must remain fully painted by the native
    # polygon and its copper palette, for all 2x2 output pixels each.
    for x, y in fill:
        for dx in (0, 1):
            for dy in (0, 1):
                ox, oy = x * 2 + dx, y * 2 + dy
                assert rgb(reference, ox, oy) == rgb(hidden, ox, oy), (x, y, dx, dy)
    outside = [(x, y) for y in range(400) for x in range(640)
               if rgb(reference, x, y) != rgb(hidden, x, y)]
    assert len(outside) > 1000, ("scanout masking never took effect", len(outside))
    assert all(378 <= x < 460 and 292 <= y < 388 for x, y in outside), outside[:12]
    print(f"native globe: {len(fill)} fill cells ({len(fill) * 4} 2x pixels) "
          f"preserved when old scanout is hidden; {len(changes)} normal "
          f"pixel differences; {len(outside)} masked non-fill differences")


if __name__ == "__main__":
    main()
