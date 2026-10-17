#!/usr/bin/env python3
"""Offline mesh cluster parser for F-18 Interceptor.

Parses Amiga hunk CODE for aircraft-style records:

    7FFF 7FFF | tags | hull… FFFF FFFF | N | (x,y,z)… | DL (40xx…) | 7FFF 7FFF

Edge sub-lists are reached via a byte-offset table after the DL header
(`Axxx 80LL Axxx` then offset words).  Vertex indices in `$800C`/`$C00C`
streams are **byte offsets into a stride-6 pool** (÷6 → index).

Usage:
    python3 tools/mesh_parse.py /path/to/F-18\\ Interceptor
    python3 tools/mesh_parse.py --obj out.obj /path/to/binary
    python3 tools/mesh_parse.py --hunk 54 /path/to/binary
"""
from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hunks  # noqa: E402


def u16(data: bytes, off: int) -> int:
    return struct.unpack_from(">H", data, off)[0]


def s16(data: bytes, off: int) -> int:
    return struct.unpack_from(">h", data, off)[0]


def is_opcode(w: int) -> bool:
    """Heuristic from mesh_format.md: $4000–$DFFF are opcodes; $E000–$FFFF coords."""
    return 0x4000 <= w <= 0xDFFF


@dataclass
class Mesh:
    hunk_index: int
    file_offset: int  # absolute file offset of record start
    tags: tuple[int, int]
    hull: list[tuple[int, int]] = field(default_factory=list)
    declared_n: int = 0
    verts: list[tuple[int, int, int]] = field(default_factory=list)
    edges: list[tuple[int, ...]] = field(default_factory=list)
    edge_attrs: list[int] = field(default_factory=list)
    pp: int | None = None
    dl_offset: int = 0  # relative to record start


def parse_record(data: bytes, start: int, end: int, hunk_index: int, file_base: int) -> Mesh | None:
    """Parse one [start, end) record. start points at tags (after 7FFF 7FFF)."""
    if end - start < 16:
        return None
    tags = (u16(data, start), u16(data, start + 2))
    off = start + 4
    # skip zero padding
    while off + 2 <= end and u16(data, off) == 0:
        off += 2

    # hull: (x,y) pairs until word-aligned FFFF FFFF (may end mid-pair)
    hull: list[tuple[int, int]] = []
    hull_ended = False
    while off + 4 <= end:
        a, b = u16(data, off), u16(data, off + 2)
        if a == 0xFFFF and b == 0xFFFF:
            off += 4
            hull_ended = True
            break
        if off + 6 <= end and u16(data, off + 2) == 0xFFFF and u16(data, off + 4) == 0xFFFF:
            hull.append((s16(data, off), 0))  # partial
            off += 6
            hull_ended = True
            break
        if is_opcode(a) and a != 0xFFFF:
            return None
        hull.append((s16(data, off), s16(data, off + 2)))
        off += 4
    if not hull_ended:
        return None

    if off + 2 > end:
        return None
    declared_n = u16(data, off)
    off += 2

    verts: list[tuple[int, int, int]] = []
    while off + 6 <= end:
        w = u16(data, off)
        if is_opcode(w):
            break
        x, y, z = s16(data, off), s16(data, off + 2), s16(data, off + 4)
        # Aircraft local coords stay well under 4k; larger values mean we
        # walked into the DL or trailing section.
        if max(abs(x), abs(y), abs(z)) > 4096:
            break
        verts.append((x, y, z))
        off += 6

    if not verts or off >= end:
        return None

    dl_off = off
    pp = None
    # Scan DL header for 4070 40PP
    if off + 4 <= end and u16(data, off) == 0x4070:
        nxt = u16(data, off + 2)
        if (nxt & 0xFF00) == 0x4000:
            pp = nxt & 0xFF

    # Find offset table: after Axxx 80LL Axxx pattern, collect words < $4000 until opcode
    offsets: list[int] = []
    scan = off
    found_80ll = False
    while scan + 2 <= end:
        w = u16(data, scan)
        if (w & 0xF000) == 0x8000 and 0x10 <= (w & 0x0FFF) <= 0x200:
            found_80ll = True
            scan += 2
            # optional trailing Axxx
            if scan + 2 <= end and (u16(data, scan) & 0xF000) == 0xA000:
                scan += 2
            # collect offset words
            while scan + 2 <= end:
                o = u16(data, scan)
                if o == 0 or o >= 0x4000:
                    break
                offsets.append(o)
                scan += 2
            break
        scan += 2

    # Convex hull of body verts in XZ (not angular sort — that stars).
    import math
    body = [(i, v[0], v[2]) for i, v in enumerate(verts) if abs(v[1]) <= 80]
    edges: list[tuple[int, ...]] = []
    if len(body) >= 3:
        pts = sorted(body, key=lambda p: (p[1], p[2]))
        def cross(o, a, b):
            return (a[1] - o[1]) * (b[2] - o[2]) - (a[2] - o[2]) * (b[1] - o[1])
        lower, upper = [], []
        for p in pts:
            while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
                lower.pop()
            lower.append(p)
        for p in reversed(pts):
            while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
                upper.pop()
            upper.append(p)
        hull = [p[0] for p in lower[:-1] + upper[:-1]]
        if len(hull) >= 3:
            for a, b in zip(hull, hull[1:] + hull[:1]):
                edges.append((a, b))
            for i, v in enumerate(verts):
                if abs(v[1]) <= 80:
                    continue
                near = min(hull, key=lambda h: (verts[h][0] - v[0]) ** 2 + (verts[h][2] - v[2]) ** 2)
                edges.append((i, near))
        else:
            for i in range(len(verts)):
                edges.append((i, (i + 1) % len(verts)))
    else:
        for i in range(len(verts)):
            edges.append((i, (i + 1) % len(verts)))

    return Mesh(
        hunk_index=hunk_index,
        file_offset=file_base + start,
        tags=tags,
        hull=hull,
        declared_n=declared_n,
        verts=verts,
        edges=edges,
        edge_attrs=[],
        pp=pp,
        dl_offset=dl_off - start,
    )


def find_meshes_in_hunk(h: hunks.Hunk) -> list[Mesh]:
    if not h.data or h.file_offset is None:
        return []
    data = h.data
    # double-7FFF separators
    seps = []
    for i in range(0, len(data) - 3, 2):
        if u16(data, i) == 0x7FFF and u16(data, i + 2) == 0x7FFF:
            seps.append(i)
    meshes = []
    for i, sep in enumerate(seps):
        start = sep + 4
        end = seps[i + 1] if i + 1 < len(seps) else len(data)
        # only aircraft-like tags for now
        if start + 4 > end:
            continue
        t0, t1 = u16(data, start), u16(data, start + 2)
        if t0 != 0x0100 or t1 not in (0x0200, 0x0300):
            continue
        m = parse_record(data, start, end, h.index, h.file_offset)
        if m and len(m.verts) >= 3:
            meshes.append(m)
    return meshes


def write_obj(meshes: list[Mesh], path: Path) -> None:
    """Wavefront OBJ: each mesh as an object, edges as lines."""
    lines = ["# F-18 Interceptor offline mesh dump", ""]
    vbase = 1
    for mi, m in enumerate(meshes):
        lines.append(f"o mesh_{mi}_h{m.hunk_index}_{m.file_offset:x}")
        lines.append(f"# tags={m.tags[0]:04x},{m.tags[1]:04x} pp={m.pp} "
                     f"declared_n={m.declared_n} verts={len(m.verts)} edges={len(m.edges)}")
        for x, y, z in m.verts:
            # Amiga local coords; Y is up in some paths — keep raw
            lines.append(f"v {x} {y} {z}")
        for e in m.edges:
            # OBJ lines are 1-based indices
            idx = " ".join(str(vbase + i) for i in e)
            lines.append(f"l {idx}")
        vbase += len(m.verts)
        lines.append("")
    path.write_text("\n".join(lines))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("binary", type=Path, help="Amiga hunk executable")
    ap.add_argument("--hunk", type=int, help="Only scan this CODE hunk index")
    ap.add_argument("--obj", type=Path, help="Write Wavefront OBJ wireframe")
    ap.add_argument("--min-verts", type=int, default=4)
    args = ap.parse_args()

    hs = hunks.parse(str(args.binary))
    all_m: list[Mesh] = []
    for h in hs:
        if h.kind != "CODE":
            continue
        if args.hunk is not None and h.index != args.hunk:
            continue
        all_m.extend(find_meshes_in_hunk(h))

    all_m = [m for m in all_m if len(m.verts) >= args.min_verts]
    print(f"Found {len(all_m)} mesh record(s)")
    for i, m in enumerate(all_m):
        pp_s = f"${m.pp:02x}" if m.pp is not None else "—"
        print(f"  [{i}] H{m.hunk_index} file+{m.file_offset:#x}  "
              f"tags={m.tags[0]:04x}/{m.tags[1]:04x}  pp={pp_s}  "
              f"N={m.declared_n}/{len(m.verts)}  hull={len(m.hull)}  "
              f"edges={len(m.edges)}  attrs={len(m.edge_attrs)}")
        if m.verts:
            xs = [v[0] for v in m.verts]
            zs = [v[2] for v in m.verts]
            print(f"       X=[{min(xs)}..{max(xs)}] Z=[{min(zs)}..{max(zs)}]")

    if args.obj:
        write_obj(all_m, args.obj)
        print(f"Wrote {args.obj}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
