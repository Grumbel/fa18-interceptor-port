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

### Display-list observations

Common aircraft pattern (e.g. `$00C39738`):

```
4070 4022 C016 4240 0022 C016 4380 0022 A244 805C A23C …
```

- `$40xx` / `$42xx` / `$43xx` / `$A2xx` appear as command words
- `$C016` recurs frequently — candidate “end-poly / close / state”
- `$80xx` words interleave later in the list
- Exact edge-index encoding **still open**.  The port keeps the raw
  `display_list` vector and uses hand-authored edges for drawing.

Some models (e.g. `$00C39B66`, `$00C39980`) start the list with low
words or a different command mix (`C0C0`, `414A`, …); may be alternate
encoding or residual hull data.

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

- [ ] Display-list opcode → edge index encoding
- [ ] Object type → `$5400` slot mapping
- [ ] Whether 2D hull pairs are used at runtime or only for tools
- [ ] Full face/poly strip interpretation
- [ ] Meaning of the frequent `$C016` command word

## Port

`src/mesh.hpp`:

- `mesh_parse()` — generic header/vertex/DL extractor with opcode heuristic
- `mesh_aircraft_39738/39800/398C6/39E00/39F66`
- `mesh_from_dump_A/B`, `mesh_structure_3A4A8`
- `draw_mesh()` — hand-authored edges (DL encoding still open)
- Each `Mesh` retains raw `display_list` for future decoder work
