#!/usr/bin/env python3
"""The capture comparison is independent of game assets and SDL2."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from analyze_line_regs import LineProbe, parse_lines, summarize


class LineRegsTest(unittest.TestCase):
    def test_negative_mode_uses_other_flag_word(self):
        self.assertEqual(LineProbe(1, 0xFF, 0xFFFF, 0x0001, 0x0000).affected_planes, 1)
        self.assertEqual(LineProbe(1, 0x0F, 0xFFFF, 0x0008, 0x0001).affected_planes, 9)
        self.assertEqual(LineProbe(1, 0x0F, 0xFFFF, 0x100B, 0x0008).affected_planes, 3)
        self.assertEqual(LineProbe(1, 0x0F, 0xFFFF, 0x0008, 0x0008).affected_planes, 0)
        self.assertEqual(LineProbe(1, 0x02, 0xFFFF, 0x0001, 0x0000).affected_planes, 0)
        self.assertEqual(LineProbe(1, 0x0F, 0x0003, 0x0008, 0x0000).affected_planes, 0)

    def test_parse_real_logger_format_and_limit_to_ram(self):
        text = [
            "running in interpreter mode\n",
            "[regs] pc=044686 frame 1329 d: 00000095 a: 0006068c "
            "ram: 05a9df:b=ff 05a9e0:w=ffff 05ac4c:w=0001 05ac4e:w=0000\n",
        ]
        row, = parse_lines(text)
        self.assertEqual((row.frame, row.mask, row.mode, row.colour, row.flags),
                         (1329, 255, 65535, 1, 0))
        self.assertIn("disagreements=1", summarize([row]))

    def test_incomplete_record_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "missing"):
            list(parse_lines(["[regs] pc=044686 frame 1 ram: 05a9df:b=ff\n"]))


if __name__ == "__main__":
    unittest.main()
