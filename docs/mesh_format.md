# Mesh / model format (in-flight)

## Pointer table

| Abs (load model) | Live | Role |
|------------------|------|------|
| `$5400` | `$00C4C4C8` | longword pointer table to mesh headers |
| | `$00C4C4F4` | companion per-slot data (stride related) |

In-flight slots:

| Slot | Pointer | Notes |
|------|---------|-------|
| 0–2 | `$00C3A58C` | mesh A (count=4) |
| 3–8 | `$00C39B52` | mesh B (count=5) |

Filled at runtime (stores at `$00C1CD7A`); consumed from `$c6` path-C
(`lea $00C4C4C8` at `$00C2537C` / `$00C260B6`).

## Header layout (best-effort)

```
word pairs …        ; 2D hull / silhouette (radar?), ends with
FFFF FFFF           ; terminator
word N              ; vertex count
N × (x.w, y.w, z.w) ; signed 16-bit local coordinates
…                   ; face/edge data (format still open)
```

### Mesh B (`$00C39B52`) — 5 verts

| # | x | y | z |
|---|----|----|----|
| 0 | 40 | 0 | -40 |
| 1 | 0 | 0 | -320 |
| 2 | -40 | 0 | -40 |
| 3 | -320 | 0 | 0 |
| 4 | -40 | 0 | 40 |

Following words look like additional verts `(0,0,320)`, `(40,0,40)`,
`(320,0,0)` — included as best-effort in the port.

### Mesh A (`$00C3A58C`) — 4 verts

| # | x | y | z |
|---|------|------|------|
| 0 | 2112 | 0 | -896 |
| 1 | 1152 | 6764 | -4192 |
| 2 | -2080 | 0 | -2048 |
| 3 | -3328 | 3720 | 480 |

Larger extents; may be terrain / carrier / distant object.

## Scale

Port uses `kMeshScale = 64` so raw coords map into the same world units
as the runway demo (transform path multiplies local verts by 64 again
for the Q2.14 matrix).

## Open

- Exact edge / face list encoding after the vertex block
- How object type index selects a `$5400` slot
- Relationship of the 2D hull pairs to the 3D verts
- Full catalogue of meshes beyond A/B

## Port

`src/mesh.hpp` — `Mesh`, `mesh_from_dump_A/B()`, `draw_mesh()`.
