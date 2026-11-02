"""Tests for the original-menu-font extractor without original game bytes."""

import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import extract_menu_font as font


class DummyHunk:
    kind = "CODE"

    def __init__(self, data):
        self.data = data


class ExtractMenuFontTest(unittest.TestCase):
    def test_checksum(self):
        self.assertEqual(font.fnv1a(b"abc"), 0x1A47E90B)

    def test_rejects_missing_or_wrong_table(self):
        with self.assertRaisesRegex(ValueError, "Hunk-61"):
            font.extract_from_hunks([])
        hunks = [DummyHunk(b"") for _ in range(font.HUNK + 1)]
        hunks[font.HUNK] = DummyHunk(bytes(font.OFFSET + font.SIZE))
        with self.assertRaisesRegex(ValueError, "does not match"):
            font.extract_from_hunks(hunks)

    def test_preview_pixels(self):
        pixels = bytearray(font.SIZE)
        pixels[0] = 0x80  # one leftmost pixel in the first glyph
        with tempfile.TemporaryDirectory() as d:
            output = Path(d) / "font.ppm"
            font.write_ppm(output, pixels, scale=1)
            contents = output.read_bytes()
            w = 8 * (font.WIDTH + 4)
            h = 7 * (font.HEIGHT + 4)
            header = f"P6\n{w} {h}\n255\n".encode("ascii")
            self.assertTrue(contents.startswith(header))
            self.assertEqual(len(contents), len(header) + w * h * 3)
            offset = len(header) + ((2 * w + 2) * 3)
            self.assertEqual(contents[offset:offset + 3], b"\xff\xc0\x40")


if __name__ == "__main__":
    unittest.main()
