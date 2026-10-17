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

    # hull: (x,y) pairs until FFFF FFFF
    hull: list[tuple[int, int]] = []
    while off + 4 <= end:
        a, b = u16(data, off), u16(data, off + 2)
        if a == 0xFFFF and b == 0xFFFF:
            off += 4
            break
        if is_opcode(a):
            return None  # not a hull-bearing cluster
        hull.append((s16(data, off), s16(data, off + 2)))
        off += 4
    else:
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

    edges: list[tuple[int, ...]] = []
    attrs: list[int] = []
    nverts = len(verts)

    def consume_edge_stream(at: int) -> int:
        """Decode $800C / $C00C edge groups starting at `at`. Return next offset."""
        p = at
        while p + 2 <= len(data):
            cmd = u16(data, p)
            p += 2
            if cmd in (0x800C, 0xC00C):
                idxs: list[int] = []
                while p + 2 <= len(data):
                    w = u16(data, p)
                    if is_opcode(w) or w == 0xFFFF:
                        break
                    # byte offset into stride-6 vertex pool
                    vi = w // 6
                    if vi < nverts:
                        idxs.append(vi)
                    p += 2
                if len(idxs) >= 2:
                    edges.append(tuple(idxs))
                # end-with-attrs forms: $8018/$801E/$8024/$802A
                if p + 2 <= len(data):
                    endcmd = u16(data, p)
                    if endcmd in (0x8018, 0x801E, 0x8024, 0x802A, 0x8000):
                        p += 2
                        if endcmd != 0x8000 and p + 2 <= len(data):
                            attrs.append(u16(data, p))
                            p += 2
                continue
            if cmd in (0x8000, 0xFFFF) or (cmd & 0xF000) == 0x4000:
                break
            if (cmd & 0xF000) == 0x8000:
                # other 8xxx — stop group
                break
            # skip unknown
            break
        return p

    # Resolve offsets relative to DL start
    for o in offsets:
        abs_off = dl_off + o
        if 0 <= abs_off < len(data):
            consume_edge_stream(abs_off)

    # Also scan for inline 800C between DL start and record end
    p = dl_off
    while p + 2 < end:
        if u16(data, p) == 0x800C:
            consume_edge_stream(p)
            p += 2
        else:
            p += 2

    # de-dup edges
    seen = set()
    uniq = []
    for e in edges:
        key = tuple(sorted(e))
        if key not in seen:
            seen.add(key)
            uniq.append(e)

    return Mesh(
        hunk_index=hunk_index,
        file_offset=file_base + start,
        tags=tags,
        hull=hull,
        declared_n=declared_n,
        verts=verts,
        edges=uniq,
        edge_attrs=attrs,
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
        if t0 not in (0x0100, 0x0000):
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
