#!/usr/bin/env python3
"""Compare native GL screenshots with original Amiga bitplane scanout on/off.

These are *display-sensitivity* counts, not a proof of native coverage:
identical colours may be drawn by different sources. Both screenshots must
come from the same deterministic frame, with normal Amiga writes enabled.

Usage: python3 tools/scanout_diff.py normal.ppm hidden.ppm [--diff heatmap.ppm]
"""

import argparse
from pathlib import Path


# Source-space bounds, left/top inclusive and right/bottom exclusive.
REGIONS = (
    ("whole-centre", (0, 0, 320, 200)),
    ("upper-screen", (0, 0, 320, 133)),
    ("front-cockpit", (0, 133, 320, 145)),
    ("lower-panel", (0, 145, 320, 200)),
    ("left-attitude", (34, 146, 101, 194)),
    ("fuel", (102, 153, 125, 195)),
    ("radar", (134, 146, 185, 185)),
    ("globe", (189, 146, 230, 194)),
    ("readouts", (230, 150, 284, 195)),
    ("status", (284, 158, 320, 199)),
)


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    with path.open("rb") as f:
        if f.readline() != b"P6\n":
            raise ValueError(f"{path}: expected a P6 screenshot")
        w, h = map(int, f.readline().split())
        if f.readline() != b"255\n":
            raise ValueError(f"{path}: expected 8-bit RGB")
        data = f.read()
    if w <= 0 or h <= 0 or len(data) != w * h * 3:
        raise ValueError(f"{path}: truncated or invalid image dimensions")
    return w, h, data


def difference_cells(w: int, h: int, original: bytes, hidden: bytes) -> bytes:
    """One byte per source pixel; nonzero if any scaled output pixel differs."""
    if len(original) != w * h * 3 or len(hidden) != len(original):
        raise ValueError("screenshots have different data sizes")
    if h % 200:
        raise ValueError("screenshot height must be a multiple of 200")
    scale = h // 200
    if w < 320 * scale or (w - 320 * scale) % 2:
        raise ValueError("screenshot must contain a centred 320x200 viewport")
    left = (w - 320 * scale) // 2
    changed = bytearray(320 * 200)
    a, b = memoryview(original), memoryview(hidden)
    for y in range(200):
        for x in range(320):
            px = left + x * scale
            different = False
            for yy in range(y * scale, (y + 1) * scale):
                start = (yy * w + px) * 3
                end = start + scale * 3
                if a[start:end] != b[start:end]:
                    different = True
                    break
            changed[y * 320 + x] = different
    return bytes(changed)


def region_counts(changed: bytes) -> list[tuple[str, int, int]]:
    if len(changed) != 320 * 200:
        raise ValueError("expected 320x200 comparison cells")
    result = []
    for name, (x0, y0, x1, y1) in REGIONS:
        count = sum(sum(changed[y * 320 + x0:y * 320 + x1])
                    for y in range(y0, y1))
        result.append((name, count, (x1 - x0) * (y1 - y0)))
    return result


def heatmap(changed: bytes) -> bytes:
    return b"".join(b"\xff\x40\x40" if bit else b"\x20\x20\x20"
                    for bit in changed)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("original", type=Path)
    p.add_argument("hidden", type=Path)
    p.add_argument("--diff", type=Path, help="write 320x200 PPM: red=changed, gray=equal")
    args = p.parse_args()
    w, h, original = read_ppm(args.original)
    other_w, other_h, hidden = read_ppm(args.hidden)
    if (w, h) != (other_w, other_h):
        p.error("image dimensions differ")
    try:
        changed = difference_cells(w, h, original, hidden)
    except ValueError as exc:
        p.error(str(exc))
    print(f"original-bitplane display sensitivity, centre 320x200 at {h // 200}x")
    print("region                 changed / total    percent")
    for name, count, total in region_counts(changed):
        print(f"{name:20s} {count:7d} / {total:5d}    {count / total * 100:5.1f}%")
    if args.diff:
        args.diff.write_bytes(b"P6\n320 200\n255\n" + heatmap(changed))
        print(f"heatmap: {args.diff}")


if __name__ == "__main__":
    main()
