#!/usr/bin/env python3
"""Synthetic-layout tests; no copyrighted Amiga executable is required."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from reference_addr import Crosswalk


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


if __name__ == "__main__":
    unittest.main()
