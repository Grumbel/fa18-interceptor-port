# Mesh / model format (in-flight)

## Pointer table

| Abs (load model) | Live | Role |
|------------------|------|------|
| `$5400` | `$00C4C4C8` | longword pointer table to mesh headers |
| | `$00C4C4F4` | companion per-slot data |

In-flight slots point into the **model cluster** `$00C39700`–`$00C3A600`.

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
matching wing/tail tips past `N` until the first opcode word:

| Top bits | Typical | Role (hypothesis) |
|----------|---------|-------------------|
| `$4xxx` | `$4070` `$4022` | draw / poly command |
| `$8xxx` | `$80E4` `$805C` | move / state |
| `$Cxxx` | `$C016` `$C0B4` | end-poly / special |

Exact edge encoding inside the display list is **still open**.  The port
uses hand-authored edges derived from vertex positions.

## Model cluster catalogue

| Address | Declared N | Usable verts | Notes |
|---------|------------|--------------|-------|
| `$00C39738` | 5 | **10** | aircraft cross ±500 |
| `$00C39800` | 5 | **10** | aircraft cross ±437/525 |
| `$00C398C6` | 4 | 8 | smaller craft |
| `$00C39980` | 4 | 8 | variant |
| `$00C39B66` | 5 | 10 | table-slot mesh B (±320) |
| `$00C39E00` | 5 | ~10 | y≠0 (banked?) |
| `$00C39F66` | 5 | ~10 | long nose −2080 |
| `$00C3A4A8` | 4 | 8 | larger structure |
| `$00C3A5A0` | 4 | 8 | mesh A (table slot) |

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

## Port

`src/mesh.hpp` — `mesh_aircraft_39738/39800`, `mesh_from_dump_A/B`, `draw_mesh()`.
