#!/usr/bin/env python3
"""Preview a moving red/blue cockpit globe from the user's original ADF.

This is an offline *candidate*, not the original game's recovered animation
algorithm. It changes only the 358 verified fill cells of pix/inst5; the
instrument bezel, marks, and background are left as in the original image.

  python3 tools/preview_globe.py ORIGINAL.adf /tmp/globe.png \
    --bank 25 --offset 1.5 --scale 4
"""

import argparse
import math
from pathlib import Path

from adf import Adf
from extract_cockpit import decode_ilbm, encode_png


RED = bytes((128, 0, 0))
BLUE = bytes((64, 64, 112))
CX, HORIZON_Y = 209.0, 173.0


def render(width: int, height: int, rgba: bytes, bank: float, offset: float,
           scale: int) -> bytes:
    if width != 320 or height != 55 or len(rgba) != width * height * 4:
        raise ValueError("expected 320x55 RGBA pix/inst5 source")
    if not 1 <= scale <= 16:
        raise ValueError("scale must be 1..16")
    out = bytearray(width * scale * height * scale * 4)
    s, c = math.sin(math.radians(bank)), math.cos(math.radians(bank))
    for y in range(height):
        for x in range(width):
            p = (y * width + x) * 4
            source = rgba[p:p + 4]
            colour = source[:3]
            fill = (198 <= x < 221 and 163 <= y + 145 < 184
                    and colour in (BLUE, RED) and source[3] != 0)
            for yy in range(scale):
                for xx in range(scale):
                    pixel = source
                    if fill:
                        px = x + (xx + 0.5) / scale
                        py = y + 145 + (yy + 0.5) / scale
                        distance = (px - CX) * s + (py - HORIZON_Y) * c - offset
                        sky = max(0.0, min(1.0, 0.5 - distance * scale))
                        pixel = bytes(round(BLUE[k] * sky + RED[k] * (1 - sky))
                                      for k in range(3)) + source[3:4]
                    dst = ((y * scale + yy) * (width * scale) + x * scale + xx) * 4
                    out[dst:dst + 4] = pixel
    return bytes(out)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("adf", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--bank", type=float, default=0.0,
                        help="candidate bank angle in degrees")
    parser.add_argument("--offset", type=float, default=0.0,
                        help="signed horizon displacement in source pixels")
    parser.add_argument("--scale", type=int, default=4, help="preview enlargement (1..16)")
    args = parser.parse_args()
    disk = Adf(args.adf)
    entries = {p: sector for p, sector, size in disk.walk() if size is not None}
    w, h, rgba = decode_ilbm(disk.read_file(entries["pix/inst5"]))
    image = render(w, h, rgba, args.bank, args.offset, args.scale)
    args.output.write_bytes(encode_png(w * args.scale, h * args.scale, image))
    print(f"globe preview: {args.output} ({w * args.scale}x{h * args.scale})")


if __name__ == "__main__":
    main()
