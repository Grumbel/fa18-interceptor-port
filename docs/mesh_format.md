# Object model format

Verified against the translated draw path (`src/native/render.cpp`,
`src/native/ops_body.inc`); addresses are runtime addresses
(`LOAD_BASE` = $20000).  The offline decoder of the mesh viewer
(`src/native/mesh_viewer.cpp`) follows exactly this description and runs
the original op handlers, so it is the executable reference.

(An earlier version of this file described "7FFF clusters" in hunk 54 and
FS-UAE dump addresses; that analysis was wrong: those words are parts of
the vertex blocks and polygon lists below.)

## Type table `$36094`

20 bytes per object type, indexed by the type number (`$5acac`):

| Off | Size | Use |
|-----|------|-----|
| +0  | long | draw routine (`jsr (a2)` in `$2fc14`) |
| +4  | long | model base: `a0` at entry, frame `-44(a6)`; all 12-bit offsets below are relative to it |
| +8  | long | LOD list → `$5ad2e` |
| +12 | long | alternative LOD list → `$5ad32` (used by `$31c9c`) |
| +16 | long | second alternative (0 for most types) |

The table is static (hunk 16 with relocations).  Types 17-117 are filled in
the executable: buildings, bridges, runways, airfields, lights, hills
(draw `$31c98`/`$31d64`/`$31c8c`) and flat ground patches such as islands,
lakes and forests (`$32d64`/`$32d72`).

Types 0-15 are zero on disk: they belong to the 16 scenario records at
`$5b47c` (512 bytes each, `$5acae` = index × 512).  When a mission creates
scenario object k, one of 16 templates at **`$35f54`** (same 20-byte
layout) is copied into type slot k (`$2d594` for the player's aircraft with
the draw routine forced to `$31c9c`, `$37762`, `$3d0dc`, `$3d320`).  These
templates hold the aircraft (F-18 variants, MiGs, airliner), ships,
missiles and the parachute.

Draw routines:

| Entry | Use |
|-------|-----|
| `$31d64` | `render_object`: link a6,#-152 and the body `$31d6c` |
| `$31c98` | `bra $31d64` |
| `$31c8c` | only below an altitude, then `$31d64` |
| `$31c9c` | scenario objects (player view / LOD list choice), then `$31d64` |
| `$369cc`, `$36a26`, `$36ac6` | scenario templates: damage/state dependent LOD list (record +76), then `$31c9c` |
| `$32d64`, `$32d72` | distant ground objects (`distant_body`, `$32d7e`), altitude gated |

## LOD list (`$31da4`)

Words read from `$5ad2e`; D = distance limit (`-40(a6)`, from `$5ae38`),
thresholds are `(w & $3fff) >> $5adb0`:

| Entry | Meaning |
|-------|---------|
| `w < 0` | descriptor `w` itself, draw stream follows |
| `thr\|$4000, desc, x` | `thr > D`: use `desc`, stream after it; else skip all three words |
| `thr, next, desc` | `thr > D`: use `desc`, stream after it; else `next` < 0: nothing, otherwise continue at base + `next` |

Descriptor: `$FFFF` = nothing; bits 0-11 = mesh header offset (`$5ad2a`);
bit 14 clear = give up when the first vertex lies outside the view
(`-98(a6)`); bit 13 = not drawn in some scenario modes (`$5ab5c`).  So the first matching entry is the nearest
detail level.

## Mesh header (`$5ad2a`)

| Off | Size | Use |
|-----|------|-----|
| +0  | word | low byte → `-136(a6)` (scenario record +123) |
| +2, +4 | word | distance limits selecting the vertex shift (`-6`/`-8`) |
| +6  | byte | two 4-bit vertex shifts |
| +7  | byte | flags, see below |
| +8  | byte | vertex count n |
| +9  | byte | articulated models: vertices also projected into the pool |
| +10 |      | n vertices: `x,y,z` words, or `x,z` words for flat models |

Flags: bit 0 = articulated scenario object (`$31f36`: vertices rotated by
the record's own matrix at +146 into record +164, the first `+9` of them
also projected), bit 1 = flat model (y = object height), bit 2 = flat
extrusion runs follow the vertices, bit 3 = 3D vertex runs follow the
vertices, bits 1+6 = occlusion test `$33fda`.

Projected vertices go to the pool at **`$5d684`**, 6 bytes each, in
vertex order.  All vertex references in the streams are byte offsets into
this pool (vertex index × 6).

Extra runs after the vertex block:

- 3D (`$324da`): `n, P, Q₁, c₁, …` — vertex P, then n more vertices, each
  with a count c; c > 1 inserts c-1 interpolated points.  The first pass
  fills every other pool slot; a following word (0 = end; bit 15 = start
  one slot back) runs the next chain backwards into the slots in between.
  Repeats while the next word is > 0.
- flat (`$32c3e`): `n, n × (x,z), t, 2 × (x,z)` — the n vertices are
  stored twice, the second copy moved by the difference of the last two
  pool vertices (an extrusion); repeats until n or t is negative.

## Draw stream (`$32666`)

Read from the word after the descriptor:

| Word | Meaning |
|------|---------|
| `v ≥ 0, f` | plane node: `f & $FC00 = $FC00` → always the first offset; `f & $0C00` → plane from the header; `f & $2000` → 6 words (point + normal); else 2 vertex offsets.  Then two base-relative offsets: visible side / other side |
| `$FFFF` | end |
| `$Axxx` | (bit 13) one polygon list at base + `xxx` (bit 12: absolute long follows), then the object ends |
| `$8xxx` | list of polygon-list references at base + `xxx` (bit 12: absolute long); each word is a list offset (bit 12: long follows), bit 15 marks the last.  Bit 14 of the group word: last group |

## Polygon list (`$3285a`)

    loop:
      w < 0:   op word
      else:    range: w = 32767 always; otherwise w and a second word
               select a distance band (near: second word < 0)
               n [, offset]   n > 0: project n more vertices from header
                              +10+offset into the pool at the same offset
                              (flat: pool offset × 1.5)
               op word

Op word: bits 0-13 = byte offset into the jump table **`$33478`**
(78 entries, `$000`-`$134`), bit 14 = last op of the list.  A handler
returning a negative d0 (e.g. a back-facing polygon without back colour)
abandons the rest of the list.  The handlers are in `ops_body.inc`.

Colour words: low 4 bits = colour (`$5ac4c`).  Where a facing test follows
(`$3383e`), a colour word ≥ 0 is tested: bits 12/13 → a plane offset word
follows (`$32adc`), bit 14 → a back-face colour word follows; negative =
always drawn.  Some ops write the plane mask/value words `$5a9de`-`$5a9e4`
first (see `remaster::poly_ops`): e.g. `$341e2` / `$343c8` XOR plane 1,
`$347ec` sets plane 3, `$3309e` sets planes 0-1 only.

Ops used by the models (args after the op word, v = pool offset):

| Off | Handler | Args | Draws |
|-----|---------|------|-------|
| $00 | `$335b0` | – | nothing |
| $04 / $e8 | `$335b4` / `$335d6` | v, colour | marker |
| $08 / $c0 | `$33744` / `$3372c` | v, v, colour | line |
| $0c | `$337ec` | v… (last \| $8000), colour [+plane] [+back] | polygon |
| $10 | `$33936` | threshold … | ground shadow from record +164 |
| $18 | `$33e74` | n, v, v, v, v, colour | n-1 rungs between two edges |
| $1c / $20 | `$34d2a` / `$34e16` | 5 / 6 words | view-dependent face |
| $24 / $6c / $78 | `$34c1c` / `$345d0` / `$345de` | colour, v | quad v0, v1, v2, v2-v1+v0 |
| $28 | `$34c8c` | colour, v | parallelogram |
| $2c / $30 | `$34968` / `$349b6` | count\|colour, v [, v] | line sets |
| $34 / $38 | `$34a3c` / `$34aa8` | colour, [v,] pairs (last \| $8000) | lines |
| $3c / $40 | `$34b2a` / `$34b9e` | colour, 4 / 3 v | quad |
| $44 | `$347ec` | (v,v,v,v)… until negative | quads |
| $4c / $50 / $54 / $58 / $e4 | `$34094` `$341e2` `$343c8` `$344f8` `$343b2` | … | subdivided patches |
| $60 / $64 / $68 | `$32ffa` / `$33052` / `$3309e` | n, … | polygon (listed / sequential vertices) |
| $7c | `$33fba` | v | visibility test |
| $88 / $8c / $90 / $a4 | `$34654` `$346a0` `$34708` `$34f76` | v [, shift] | derive more pool vertices |
| $9c / $a0 | `$33b60` / `$33d22` | long [, word] | view-dependent box sides |
| $118 / $124 | `$335f8` / `$33664` | – | skip by animation state / type 87 |

## Distant objects (`$32db4`)

LOD list entries: `w < 0` → mesh pointer (long) and stream follow;
`thr, next.l, mesh.l, stream…` — `thr ≥ D` uses `mesh`, otherwise continue
at `next` (< 0: nothing).  Mesh: vertex count word, 4 bytes, then `x,z`
pairs.  The stream is a polygon list as above (without plane nodes); the
range form `w < D` continues at a long pointer.

## Viewer

Debug menu (Insert / Back) → **Mesh viewer**: all 16 templates (labelled
S1-S16) and types 17-117, identical decodes merged (111 models).
Left/Right model, PgUp/PgDn ±10, Up/Down LOD, WASD / right stick orbit,
+/- / triggers zoom, Tab / X outlines, P / Y live machine palette instead
of the captured flight palette.  `fa18 --mesh-dump DIR` prints every model
and LOD and writes one PPM each (`FA18_MESH_PRIMS=1` also prints the
primitives).

The decoder runs in object space: vertices go to the pool unrotated with a
depth offset (so the ops' "behind the camera" tests pass), both sides of
every plane node are followed, D = the LOD's distance, the facing test
always reports "front", and calls into the rasteriser (`$38c0a`/`$38c0c`,
`$43a56`/`$4397c`, `$438a0`…) are captured as polygons, lines and points.
Masked / XOR primitives (shadows, ground patches) are shown on the land
colour.
