#!/usr/bin/env python3
"""Extract the two original cockpit/HUD ILBM assets as RGBA PNGs from an ADF.

The generated pictures remain user-supplied game data, outside the repository.
The static artwork is only one part of the cockpit: the game draws gauges,
texts and indicators dynamically over these bitmaps.

Usage: python3 tools/extract_cockpit.py game.adf /tmp/cockpit-png
"""

import argparse
import pathlib
import struct
import zlib

from adf import Adf


def decode_byterun1(data: bytes, expected: int) -> bytes:
    out = bytearray()
    i = 0
    while len(out) < expected:
        if i >= len(data):
            raise ValueError("truncated ByteRun1 stream")
        n = struct.unpack_from("b", data, i)[0]
        i += 1
        if n >= 0:
            count = n + 1
            if i + count > len(data):
                raise ValueError("truncated ByteRun1 literal")
            out.extend(data[i:i + count])
            i += count
        elif n != -128:
            if i >= len(data):
                raise ValueError("truncated ByteRun1 repeat")
            out.extend(data[i:i + 1] * (1 - n))
            i += 1
        if len(out) > expected:
            raise ValueError("ByteRun1 exceeds declared image dimensions")
    return bytes(out)


def decode_ilbm(data: bytes):
    if data[:4] != b"FORM" or data[8:12] != b"ILBM":
        raise ValueError("expected FORM ILBM")
    form_end = min(len(data), 8 + struct.unpack_from(">I", data, 4)[0])
    chunks = {}
    p = 12
    while p + 8 <= form_end:
        name = data[p:p + 4]
        size = struct.unpack_from(">I", data, p + 4)[0]
        p += 8
        if p + size > form_end:
            raise ValueError("truncated ILBM chunk")
        chunks[name] = data[p:p + size]
        p += size + (size & 1)
    bmhd = chunks[b"BMHD"]
    w, h, _, _, planes, masking, compression, _, transparent, _, _, _, _ = \
        struct.unpack_from(">HHhhBBBBHBBhh", bmhd)
    if not (0 < w <= 4096 and 0 < h <= 4096 and 1 <= planes <= 8):
        raise ValueError("unsupported ILBM dimensions/planes")
    if masking not in (0, 2) or compression not in (0, 1):
        raise ValueError("unsupported ILBM masking/compression")
    row_bytes = ((w + 15) // 16) * 2
    length = row_bytes * planes * h
    body = chunks[b"BODY"]
    planar = decode_byterun1(body, length) if compression else body
    if len(planar) != length:
        raise ValueError("invalid ILBM BODY length")
    cmap = chunks[b"CMAP"]
    palette = [cmap[i:i + 3] for i in range(0, len(cmap) - 2, 3)]
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            index = 0
            for plane in range(planes):
                off = (y * planes + plane) * row_bytes + x // 8
                index |= ((planar[off] >> (7 - (x & 7))) & 1) << plane
            rgb = palette[index] if index < len(palette) else b"\0\0\0"
            at = (y * w + x) * 4
            out[at:at + 4] = rgb + bytes([0 if masking == 2 and index == transparent else 255])
    return w, h, bytes(out)


def encode_png(w: int, h: int, rgba: bytes) -> bytes:
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))

    scanlines = b"".join(b"\0" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(scanlines, 9)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("adf")
    parser.add_argument("output_dir", type=pathlib.Path)
    args = parser.parse_args()
    disk = Adf(args.adf)
    entries = {path: sector for path, sector, size in disk.walk() if size is not None}
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name in ("pix/inst5", "pix/frnt5"):
        w, h, rgba = decode_ilbm(disk.read_file(entries[name]))
        out = args.output_dir / (name.rsplit("/", 1)[1] + ".png")
        out.write_bytes(encode_png(w, h, rgba))
        print(f"{out}: {w}x{h} RGBA")


if __name__ == "__main__":
    main()
