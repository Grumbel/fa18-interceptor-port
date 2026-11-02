"""Ensure static recompiler dispatch barriers track the OpenGL PC hooks."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class RecompHooksTest(unittest.TestCase):
    def test_hires_hooks_dispatch_in_recompiled_mode(self):
        source = (ROOT / "src/native/hires.cpp").read_text()
        static_hooks = {
            int(addr, 16)
            for addr in re.findall(r"m\.add_pc_hook\(0x([0-9a-fA-F]+),", source)
        }
        hooks_file = (ROOT / "tools/recomp_hooks.txt").read_text()
        recomp_hooks = {
            int(line.split("#", 1)[0].strip(), 16)
            for line in hooks_file.splitlines()
            if line.split("#", 1)[0].strip()
        }
        # $2126e is the dynamic hook installed for the original scene's
        # observed return address; all other hires hooks are static.
        self.assertEqual(recomp_hooks, static_hooks | {0x2126E})


if __name__ == "__main__":
    unittest.main()
