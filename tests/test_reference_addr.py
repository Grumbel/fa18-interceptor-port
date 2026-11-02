#!/usr/bin/env python3
"""Synthetic-layout tests; no copyrighted Amiga executable is required."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from reference_addr import Crosswalk, index_notes, scan_reference_addresses


class CrosswalkTest(unittest.TestCase):
    def setUp(self):
        inventory = {"segments": [
            {"index": 0, "kind": "CODE", "size_bytes": 32},
            {"index": 1, "kind": "BSS", "size_bytes": 8},
            {"index": 2, "kind": "CODE", "size_bytes": 16},
            {"index": 3, "kind": "CODE", "size_bytes": 8},
        ]}
        mapping = {"resolved": {
            "0": {"runtime_payload_base": 0xC00000, "status": "verified"},
            "1": {"runtime_payload_base": 0xC10000, "status": "bss_unverified"},
            "2": {"runtime_payload_base": 0xC20000, "status": "verified"},
        }}
        self.crosswalk = Crosswalk(inventory, mapping)

    def test_forward_and_reverse(self):
        m, = self.crosswalk.translate(0xC2000C, "reference")
        self.assertEqual((m.hunk, m.offset, m.port), (2, 12, 0x2004C))
        self.assertEqual(self.crosswalk.translate(m.port, "port"), [m])

    def test_eight_byte_headers_and_bss(self):
        self.assertEqual(self.crosswalk.port_bases,
                         {0: 0x20008, 1: 0x20030, 2: 0x20040, 3: 0x20058})
        m, = self.crosswalk.translate(0x20033, "port")
        self.assertEqual((m.reference, m.status), (0xC10003, "bss_unverified"))
        self.assertEqual(self.crosswalk.translate(0x2003F, "port"), [])

    def test_unresolved_and_out_of_range(self):
        self.assertEqual(self.crosswalk.translate(0x2005A, "port"), [])
        self.assertEqual(self.crosswalk.translate(0xC20010, "reference"), [])
        self.assertEqual(self.crosswalk.translate(0x20007, "port"), [])

    def test_requires_contiguous_inventory(self):
        with self.assertRaises(ValueError):
            Crosswalk({"segments": [{"index": 1, "size_bytes": 4}]},
                      {"resolved": {}})

    def test_index_notes_uses_filename_addresses_only(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            notes = root / "analysis/routines"
            notes.mkdir(parents=True)
            (notes / "c2000c_example.md").touch()  # Hunk 2 + $C
            (notes / "c2001c_outside.md").touch()
            (notes / "misc.md").touch()
            # The routine-note naming convention uses six hex digits including
            # the leading C; this synthetic map deliberately uses C2xxxx.
            self.crosswalk.reference[2]["runtime_payload_base"] = 0xC20000
            rows = list(index_notes(root, self.crosswalk))
            self.assertEqual(len(rows), 1)
            match, path = rows[0]
            self.assertEqual(match.port, 0x2004C)
            self.assertEqual(str(path), "analysis/routines/c2000c_example.md")

    def test_scan_source_addresses_ignores_comments_hardware_and_duplicates(self):
        source = [
            "; old address $C2FFFF must not enter the crosswalk\n",
            "START equ $C2000C ; stale $C20003\n",
            "\tbsr.w\t$c00003 ; and $DFF000 is hardware\n",
            " move.l 0xC2000C,a0\n",
            " jsr $C20003\n",
        ]
        rows = list(scan_reference_addresses(source))
        self.assertEqual([(addr, line) for addr, line, _ in rows],
                         [(0xC2000C, 2), (0xC00003, 3), (0xC20003, 5)])
        self.assertEqual([self.crosswalk.translate(addr, "reference")[0].port
                          for addr, _, _ in rows], [0x2004C, 0x2000B, 0x20043])
        self.assertEqual(rows[1][2], "bsr.w $c00003")  # stable TSV field, no tab


if __name__ == "__main__":
    unittest.main()
