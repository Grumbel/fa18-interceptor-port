#!/usr/bin/env python3
"""Compare observed Hunk-36 line-plane flags with the hi-res colour model.

Input is stderr/stdout from FA18_REGS=44686,N,5a9df:b,5a9e0:w,5ac4c:w,5ac4e:w.
The independent blitter-line reconstruction selects the low byte of $5AC4E
when $5A9E0 is negative. Current native/hires.cpp::line_ops() selects
$5AC4C instead. No original game bytes or complete logs are stored here.

Usage: python3 tools/analyze_line_regs.py /tmp/fa18-line-probe.log
       ... | python3 tools/analyze_line_regs.py -
"""

import argparse
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import re
import sys


FIELD_RE = re.compile(r"\b([0-9a-fA-F]+):([bwl])=([0-9a-fA-F]+)\b")
FRAME_RE = re.compile(r"\bframe\s+(\d+)\b")
REQUIRED = (0x5A9DF, 0x5A9E0, 0x5AC4C, 0x5AC4E)


@dataclass(frozen=True)
class LineProbe:
    frame: int
    mask: int
    mode: int
    colour: int
    flags: int

    @property
    def negative(self):
        return (self.mode & 0x8000) != 0

    @property
    def affected_planes(self):
        if not self.negative:
            return 0  # Both implementations use $5A9E0's low byte.
        return (self.colour ^ self.flags) & self.mask & 0x0F


def parse_lines(lines):
    for lineno, line in enumerate(lines, 1):
        if not line.startswith("[regs]"):
            continue
        frame = FRAME_RE.search(line)
        fields = {int(addr, 16): int(value, 16)
                  for addr, _width, value in FIELD_RE.findall(line)}
        missing = [f"${addr:06X}" for addr in REQUIRED if addr not in fields]
        if not frame or missing:
            raise ValueError(f"line {lineno}: missing frame or probe fields {', '.join(missing)}")
        yield LineProbe(int(frame.group(1)), *(fields[a] for a in REQUIRED))


def summarize(records):
    records = list(records)
    if not records:
        return "No [regs] line-renderer samples found."
    negative = sum(r.negative for r in records)
    affected = [r for r in records if r.affected_planes]
    grouped = Counter((r.mask & 0xF, r.colour & 0xF, r.flags & 0xF,
                       r.affected_planes) for r in records if r.negative)
    lines = [
        f"calls={len(records)} frames={len({r.frame for r in records})} "
        f"negative_mode={negative} positive_mode={len(records) - negative}",
        f"hires/reference plane-bit disagreements={len(affected)} "
        f"(active display planes only)",
    ]
    if affected:
        lines.append("first affected frames=" +
                     ",".join(str(f) for f in sorted({r.frame for r in affected})[:12]))
    lines.append("negative-mode cases: mask colour flags differing_planes count")
    for (mask, col, flags, delta), count in sorted(grouped.items(),
                                                   key=lambda item: (-item[1], item[0])):
        lines.append(f"                    {mask:01x}    {col:01x}      {flags:01x}      "
                     f"{delta:01x}               {count}")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="FA18_REGS log file, or '-' for stdin")
    args = parser.parse_args()
    if str(args.log) == "-":
        print(summarize(parse_lines(sys.stdin)))
    else:
        with args.log.open(encoding="utf-8") as f:
            print(summarize(parse_lines(f)))


if __name__ == "__main__":
    main()
