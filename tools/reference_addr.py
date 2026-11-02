#!/usr/bin/env python3
"""Translate retro-foundry's live Amiga addresses into this port's addresses.

The other project records Hunk payload bases in a particular Engine9000
snapshot.  This port instead loads every Hunk consecutively from $20000 with
an eight-byte LoadSeg header.  Hunk index + payload offset is the invariant.

Usage:
  python3 tools/reference_addr.py ../fa18interceptor --from-reference 0xc1718e
  python3 tools/reference_addr.py ../fa18interceptor --from-port 0x292e6
  python3 tools/reference_addr.py ../fa18interceptor --index
  python3 tools/reference_addr.py ../fa18interceptor --executable /path/to/exe \
      --from-reference 0xc2fa7e

The optional executable check validates Hunk layout only, not binary identity.
"""

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path


LOAD_BASE = 0x20000


@dataclass(frozen=True)
class Match:
    hunk: int
    offset: int
    reference: int
    port: int
    status: str


class Crosswalk:
    def __init__(self, inventory, mapping):
        segments = inventory["segments"]
        if [s["index"] for s in segments] != list(range(len(segments))):
            raise ValueError("expected contiguous, zero-based Hunk indices")

        self.segments = segments
        self.reference = {int(k): v for k, v in mapping["resolved"].items()}
        self.port_bases = {}
        cursor = LOAD_BASE
        for segment in segments:
            self.port_bases[segment["index"]] = cursor + 8
            cursor += 8 + segment["size_bytes"]

    def validate_executable(self, path):
        # Existing parser is the same one that drives tools/loadseg.py.
        import hunks

        actual = hunks.parse(path)
        if len(actual) != len(self.segments):
            raise ValueError(f"Hunk count mismatch: reference {len(self.segments)}, "
                             f"executable {len(actual)}")
        for observed, expected in zip(actual, self.segments):
            if observed.index != expected["index"] or (observed.kind, observed.alloc_size) != (expected["kind"], expected["size_bytes"]):
                raise ValueError(f"Hunk {expected['index']} layout mismatch: "
                                 f"reference {expected['kind']} {expected['size_bytes']} bytes, "
                                 f"executable {observed.kind} {observed.alloc_size} bytes")

    def translate(self, address, direction):
        if direction not in ("reference", "port"):
            raise ValueError(f"unknown direction: {direction}")
        matches = []
        for segment in self.segments:
            hunk = segment["index"]
            resolved = self.reference.get(hunk)
            if resolved is None:
                continue
            ref_base = resolved["runtime_payload_base"]
            port_base = self.port_bases[hunk]
            base = ref_base if direction == "reference" else port_base
            offset = address - base
            if 0 <= offset < segment["size_bytes"]:
                matches.append(Match(hunk, offset, ref_base + offset,
                                     port_base + offset, resolved["status"]))
        return matches


def read_crosswalk(root):
    inventory_path = root / "analysis/hunk_inventory.json"
    mapping_path = root / "analysis/hunk_runtime_resolved.json"
    with inventory_path.open(encoding="utf-8") as f:
        inventory = json.load(f)
    with mapping_path.open(encoding="utf-8") as f:
        mapping = json.load(f)
    return Crosswalk(inventory, mapping)


def fmt(match):
    return (f"reference ${match.reference:06X} = Hunk {match.hunk}+${match.offset:X} "
            f"= port ${match.port:05X} [{match.status}]")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("reference_root", type=Path,
                        help="checkout of github.com/retro-foundry/fa18interceptor")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--from-reference", nargs="+", metavar="ADDR",
                       help="Engine9000 runtime address(es), e.g. 0xc1718e")
    group.add_argument("--from-port", nargs="+", metavar="ADDR",
                       help="our fixed LoadSeg address(es), e.g. 0x292e6")
    group.add_argument("--index", action="store_true",
                       help="list translated analysis/routines/c*.md notes")
    parser.add_argument("--executable", type=Path,
                        help="optional original executable for Hunk layout verification")
    args = parser.parse_args()

    crosswalk = read_crosswalk(args.reference_root)
    if args.executable:
        crosswalk.validate_executable(args.executable)

    if args.index:
        print("port\thunk\treference\tstatus\tanalysis note")
        notes = args.reference_root / "analysis/routines"
        for path in sorted(notes.glob("c*.md")):
            m = re.match(r"(c[0-9a-fA-F]{5})(?:_|\.)", path.name)
            if m is None:
                continue
            for match in crosswalk.translate(int(m.group(1), 16), "reference"):
                print(f"${match.port:05X}\t{match.hunk}+${match.offset:X}\t"
                      f"${match.reference:06X}\t{match.status}\t{path.relative_to(args.reference_root)}")
        return 0

    direction = "reference" if args.from_reference else "port"
    addresses = args.from_reference if args.from_reference else args.from_port
    missing = 0
    for value in addresses:
        address = int(value.replace("$", "0x"), 0)
        matches = crosswalk.translate(address, direction)
        if matches:
            for match in matches:
                print(fmt(match))
        else:
            print(f"${address:06X}: no resolved reference Hunk (or header/gap)")
            missing += 1
    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
