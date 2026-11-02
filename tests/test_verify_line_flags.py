"""Synthetic opcode coverage for the original-Hunk line-flag verifier."""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import verify_line_flags as verifier


class LineFlagVerificationTest(unittest.TestCase):
    def make_image(self, address=verifier.NEGATIVE_FLAG_BYTE):
        opcodes = []
        for plane in range(4):
            opcodes.append(verifier.btst_immediate_absolute_long(
                plane, verifier.MODE_FLAG_BYTE))
            opcodes.append(verifier.btst_immediate_absolute_long(plane, address))
        opcodes = b"\x4e\x71".join(opcodes)
        start = verifier.LINE_START - verifier.loadseg.LOAD_BASE
        image = bytearray(verifier.LINE_END - verifier.loadseg.LOAD_BASE)
        image[start:start + len(opcodes)] = opcodes
        return image

    def test_reconstructed_selector_bytes(self):
        results = verifier.find_references(self.make_image())
        self.assertTrue(verifier.matches_reference(results))
        self.assertEqual([r.locations[0] for r in results],
                         [verifier.LINE_START + i * 10 for i in range(8)])

    def test_old_observer_address_is_not_accepted(self):
        results = verifier.find_references(self.make_image(0x5AC4C))
        self.assertFalse(verifier.matches_reference(results))
        self.assertTrue(all(not r.locations for r in results
                            if r.mode == "negative"))
        self.assertEqual([(bit, addr) for _pc, bit, addr in
                          verifier.find_other_selectors(self.make_image(0x5AC4C))],
                         [(bit, 0x5AC4C) for bit in range(4)])

    def test_missing_or_duplicate_selector_is_not_accepted(self):
        image = self.make_image()
        image[verifier.LINE_START - verifier.loadseg.LOAD_BASE] = 0
        self.assertFalse(verifier.matches_reference(verifier.find_references(image)))
        image = self.make_image()
        start = verifier.LINE_START - verifier.loadseg.LOAD_BASE
        image[start + 100:start + 108] = image[start:start + 8]
        self.assertFalse(verifier.matches_reference(verifier.find_references(image)))

    def test_rejects_window_outside_image(self):
        with self.assertRaises(ValueError):
            verifier.find_references(b"\x00" * 12)


if __name__ == "__main__":
    unittest.main()
