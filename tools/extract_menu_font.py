#!/usr/bin/env python3
"""Extract the original 5x7 menu glyph sheet from a FA-18 ADF/executable.

    python3 tools/extract_menu_font.py game.adf /tmp/fa18-font.ppm
    python3 tools/extract_menu_font.py 'F-18 Interceptor' /tmp/fa18-font.ppm

The file contains 53 glyphs in Hunk 61 at offset $1F3 (7 bytes/glyph,
five left-justified bits/row). No copyrighted font bitmap is in the repo.
"""

import argparse
from pathlib import Path
import tempfile

from adf import Adf
import hunks


HUNK = 61
OFFSET = 0x1F3
COUNT = 53
HEIGHT = 7
WIDTH = 5
SIZE = COUNT * HEIGHT
EXPECTED_FNV1A = 0x333527EF


def fnv1a(data):
    value = 2166136261
    for b in data:
        value = ((value ^ b) * 16777619) & 0xFFFFFFFF
    return value


def extract_from_hunks(parsed):
    if len(parsed) <= HUNK or parsed[HUNK].kind != "CODE":
        raise ValueError("executable has no Hunk-61 CODE segment")
    data = parsed[HUNK].data[OFFSET:OFFSET + SIZE]
    if len(data) != SIZE or fnv1a(data) != EXPECTED_FNV1A:
        raise ValueError("Hunk-61 glyph table does not match the known game version")
    return data


def extract(path):
    if path.suffix.lower() != ".adf":
        return extract_from_hunks(hunks.parse(str(path)))

    disk = Adf(str(path))
    headers = [header for name, header, size in disk.walk()
               if name.lower() == "f-18 interceptor" and size is not None]
    if len(headers) != 1:
        raise ValueError("expected one 'F-18 Interceptor' executable in the ADF")
    with tempfile.TemporaryDirectory(prefix="fa18-menu-font-") as directory:
        exe = Path(directory) / "F-18 Interceptor"
        exe.write_bytes(disk.read_file(headers[0]))
        return extract_from_hunks(hunks.parse(str(exe)))


def write_ppm(path, rows, scale=4):
    columns = 8
    tile_width, tile_height = WIDTH + 4, HEIGHT + 4
    width = columns * tile_width * scale
    height = ((COUNT + columns - 1) // columns) * tile_height * scale
    rgb = bytearray(width * height * 3)
    for index in range(COUNT):
        col, row = index % columns, index // columns
        for y in range(HEIGHT):
            bits = rows[index * HEIGHT + y]
            for x in range(WIDTH):
                if not (bits & (0x80 >> x)):
                    continue
                for sy in range(scale):
                    for sx in range(scale):
                        px = (col * tile_width + 2 + x) * scale + sx
                        py = (row * tile_height + 2 + y) * scale + sy
                        off = (py * width + px) * 3
                        rgb[off:off + 3] = b"\xff\xc0\x40"
    with path.open("wb") as f:
        f.write(f"P6\n{width} {height}\n255\n".encode("ascii"))
        f.write(rgb)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path, help="ADF or extracted Hunk executable")
    parser.add_argument("preview", type=Path, help="PPM preview sheet to create")
    parser.add_argument("--raw", type=Path, help="also extract the 371 raw glyph bytes")
    args = parser.parse_args()
    try:
        data = extract(args.game)
        write_ppm(args.preview, data)
        if args.raw:
            args.raw.write_bytes(data)
    except (OSError, ValueError, IndexError) as error:
        parser.exit(1, f"font extraction failed: {error}\n")
    print(f"{COUNT} original glyphs, 5x7, Hunk {HUNK} + ${OFFSET:X}, "
          f"FNV-1a ${fnv1a(data):08X}")
    print("Indices: 16-25 = 0-9; 26 = ':'; 27-52 = A-Z; "
          "0-15 = game punctuation/symbols")
    print(f"Preview: {args.preview}")
    if args.raw:
        print(f"Raw glyphs: {args.raw}")


if __name__ == "__main__":
    main()
