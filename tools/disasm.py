#!/usr/bin/env python3
"""Disassemble the relocated executable image with GNU objdump (m68k) and
annotate it with hunk boundaries and the library-stub symbols.

    OBJDUMP=m68k-unknown-linux-gnu-objdump \
        python3 tools/disasm.py "assets/F-18 Interceptor" > fa18.dis

objdump is available via nix:  nix build nixpkgs#pkgsCross.m68k.buildPackages.binutils
Addresses are runtime addresses of the C++ loader (LOAD_BASE = 0x20000).
"""
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(__file__))
import loadseg


def main():
    exe = sys.argv[1]
    objdump = os.environ.get("OBJDUMP", "m68k-unknown-linux-gnu-objdump")
    hunks, addrs, img = loadseg.load(exe)
    base = loadseg.LOAD_BASE
    syms = {}
    for h, a in zip(hunks, addrs):
        for n, o in h.symbols:
            if not n.startswith(".L"):
                syms[a + o] = n
    starts = {a: h for h, a in zip(hunks, addrs)}
    with tempfile.NamedTemporaryFile(suffix=".bin") as tf:
        tf.write(img)
        tf.flush()
        out = subprocess.run([objdump, "-b", "binary", "-m", "m68k:68000", "-D", tf.name],
                             capture_output=True, text=True, check=True).stdout
    line_re = re.compile(r"^ +([0-9a-f]+):\t(.*)$")
    for line in out.splitlines():
        m = line_re.match(line)
        if not m:
            continue
        a = int(m.group(1), 16) + base
        rest = m.group(2)
        if a in starts:
            h = starts[a]
            print(f"\n;---- hunk {h.index} {h.kind} size {h.alloc_size:#x} ----")
        if a in syms:
            print(f"\n{syms[a]}:")

        def fix_target(mm):
            t = int(mm.group(2), 16) + base
            return f"{mm.group(1)}0x{t:x}"

        # PC-relative operands/branches are printed relative to the file.
        rest = re.sub(r"(\b(?:b[a-z]{2,3}[sw]?|db[a-z]{1,2}|bra[sw]?|bsr[sw]?) (?:%d\d,)?)0x([0-9a-f]+)$",
                      fix_target, rest)
        rest = re.sub(r"(%pc@\()0x([0-9a-f]+)", fix_target, rest)

        def sym(mm):
            t = int(mm.group(2), 16)
            return mm.group(0) + (f" <{syms[t]}>" if t in syms else "")

        rest = re.sub(r"(jsr|jmp|bsr[sw]?) 0x([0-9a-f]+)", sym, rest)
        print(f"{a:6x}:\t{rest}")


if __name__ == "__main__":
    main()
