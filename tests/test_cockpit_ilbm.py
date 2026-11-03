#!/usr/bin/env python3
"""Synthetic ILBM decoding test; no copyrighted ADF required."""
import pathlib
import struct
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from extract_cockpit import decode_byterun1, decode_ilbm, encode_png


def chunk(name, payload):
    return name + struct.pack(">I", len(payload)) + payload + b"\0" * (len(payload) & 1)


def test_decoding():
    # Two 16-pixel rows; pixels 0-7 use index 1 (red), remaining are
    # transparent index 0. ByteRun1 has literal and repeat runs.
    assert decode_byterun1(b"\x00\x80\xfd\x7f", 5) == b"\x80" + b"\x7f" * 4
    bmhd = struct.pack(">HHhhBBBBHBBhh", 16, 2, 0, 0, 1, 2, 0, 0, 0, 1, 1, 16, 2)
    data = (chunk(b"BMHD", bmhd) + chunk(b"CMAP", b"\0\0\0\xff\0\0") +
            chunk(b"BODY", b"\xff\0\xff\0"))
    iff = b"FORM" + struct.pack(">I", len(data) + 4) + b"ILBM" + data
    w, h, rgba = decode_ilbm(iff)
    assert (w, h) == (16, 2)
    assert rgba[:4] == b"\xff\0\0\xff"
    assert rgba[8 * 4:8 * 4 + 4] == b"\0\0\0\0"
    png = encode_png(w, h, rgba)
    assert png[:8] == b"\x89PNG\r\n\x1a\n"
    assert zlib.decompress(png[png.find(b"IDAT") + 4:-16])[:5] == b"\0\xff\0\0\xff"


if __name__ == "__main__":
    test_decoding()
    print("PASS: synthetic ILBM transparency and PNG")
