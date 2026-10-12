# Mesh / model format (in-flight)

## Pointer table

| Abs (load model) | Live | Role |
|------------------|------|------|
| `$5400` | `$00C4C4C8` | longword pointer table to mesh headers |
| | `$00C4C4F4` | companion per-slot data |

In-flight slots point into the **model cluster** `$00C39700`–`$00C3A600`.

Observed live slots (first 9):

| Slot | Target |
|------|--------|
| 0–2 | `$00C3A58C` (mesh A family) |
| 3–8 | `$00C39B52` / `$00C39B66` (mesh B) |

## Header layout

```
[2D hull word-pairs…]     ; optional radar/silhouette hull
FFFF FFFF                 ; terminator
word N                    ; declared vertex count (often under-states)
(x,y,z).w × N+            ; signed local coords — stream continues past N
                          ; until a display-list opcode
display list…             ; words with high bits set ($40xx/$80xx/$C0xx)
```

### Vertex stream

Declared `N` is a lower bound.  Symmetric aircraft models continue with
matching wing/tail tips past `N` until the first opcode word.

**Opcode heuristic** (needed because negative coordinates also have bit 15 set):

| Range | Interpretation |
|-------|----------------|
| `$4000`–`$7FFF` | opcode (bit 14 set) |
| `$8000`–`$DFFF` | opcode (bit 15, not a small negative) |
| `$E000`–`$FFFF` | coordinate (small negative, \|v\| < 8192) |

### Display-list structure (partial decode)

Common aircraft pattern (e.g. `$00C39738`):

```
4070 4022 C016 4240 0022 C016 4380 0022 A244 805C A23C
0270 02D6 02E2 0330 035C          ← offset table (byte offs from mesh base)
8368 …
… 2-D silhouette coords …
7FFF 7FFF                         ← end marker
```

**Two-level encoding:**

1. **Top level** — `$40xx` / `$C016` / `$A2xx` state commands (LOD, colour,
   or draw-mode; exact meaning still open).  Followed by a short table of
   **byte offsets** relative to the mesh header.

2. **Sub-lists** at those offsets:
   ```
   800C 0006 0024 002A FFFF
   ```
   - `$8xxx` / `$Cxxx` = command word (`$800C` seen with 3 trailing indices)
   - Following words `< $100` that are multiples of **6** are vertex
     indices encoded as **byte offsets** (stride 6 = 3 × int16 per vert)
   - `$FFFF` terminates the sub-list

   Example: `0006 0024 002A` → indices 1, 6, 7.

3. **Trailing 2-D coords** — radar / silhouette hull (same format as the
   optional pre-header hull).

`mesh_edges_from_dl()` in the port implements this partial decoder and
recovers a useful subset of edges.  Hand-authored edges remain the
primary draw path until top-level `$40xx`/`$C016` is fully understood.

Some models (e.g. `$00C39B66`, `$00C39980`) use a different top-level mix
(`C0C0`, `414A`, …); the sub-list format still applies where offsets are
present.

## Model cluster catalogue

| Address | Declared N | Usable verts | Notes |
|---------|------------|--------------|-------|
| `$00C39738` | 5 | **10** | aircraft cross ±500 |
| `$00C39800` | 5 | **10** | aircraft cross ±437/525 |
| `$00C398C6` | 4 | **8** | smaller craft |
| `$00C39980` | 4 | **8** | variant (DL style differs) |
| `$00C39B66` | 5 | **10** | table-slot mesh B (±320) |
| `$00C39E00` | 5 | **10** | Y≠0 body ring (banked?) |
| `$00C39F66` | 5 | **11** | long nose ±2080 |
| `$00C3A4A8` | 4 | **8** | larger structure |
| `$00C3A5A0` | 4 | **8** | mesh A (table slot) |

### Aircraft topology (e.g. `$00C39738`)

```
        v1 nose (0,0,-500)
       / | \
     v0  v9  v2     body ring + center-up
    /    |    \
  v3-----|------v7  wings (±500,0,0)
    \    |    /
     v6  |  v4
       \ | /
        v5 tail (0,0,+500)
         v8 vertical (40,400,-40)
```

## Scale

Port: `kMeshScale = 64` so raw coords fit the runway world units.
Transform path multiplies by 64 again for Q2.14 matrix input.

## Open

- [~] Display-list → edge encoding: **sub-list byte-offset/6 decoded**;
      top-level `$40xx`/`$C016` state commands still open
- [ ] Object type → `$5400` slot mapping
- [ ] Whether 2D hull pairs are used at runtime or only for tools
- [ ] Full face/poly strip interpretation (cmd word low-nibble = count?)
- [ ] Meaning of the frequent `$C016` command word

## Port

`src/mesh.hpp`:

- `mesh_parse()` — generic header/vertex/DL extractor with opcode heuristic
- `mesh_edges_from_dl()` — partial decoder (sub-list byte-offset indices)
- `mesh_aircraft_39738/39800/398C6/39E00/39F66`
- `mesh_from_dump_A/B`, `mesh_structure_3A4A8`
- `draw_mesh()` — hand-authored edges (primary); DL decoder supplements
- Each `Mesh` retains raw `display_list` for future decoder work
