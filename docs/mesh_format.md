# Mesh / model format (in-flight)

## Pipeline overview

```
Object type
    │
    ▼
$00C21000 dispatch table ──► 3D cluster DL  ($00C39700+)   ← raw form with PP
    │                        LOD table       (count+ptrs+hull+mesh)
    │                        Instance pool   ($00C43700+, stride $6A)
    │                        Walker trampoline ($00C1D8A0 → $00C1D96C)
    │                        Drawable map     ($00C20B94+)  ← alternate DLs
    │
    ▼
LOD/proximity ($00C4C4C8 slot table, d5 countdown, threshold 320)
    │
    ├─► Hull → companion ($00C1CD38 transform, stride 256)
    │
    └─► Select drawable stream (raw cluster OR alternate $00C37xxx)
            dual ptrs: $00C4476E (a2) + $00C44772 (secondary)
            │
            ▼
        Display list walk ($00C1D96C / $00C08232)
            bit15 → edge cmds ($800C verts, $8018+attrs, $80LL end)
            bit14 → $4xxx skip-next (param word)
            else  → threshold filter / in-list offset
            │
            ▼
        Pen from a2 stream → $5ee line draw
            (or jump-table path → edge-list $00C4ACC8 → DDA)
```

**Known gaps:** PP (`$22`/`$20`/`$BA` / `$60PP`) / `$70` consumer (no
runtime path found; likely offline/asset-build).  Pure-DL streams in
`$35xxx`/`$36xxx` need the live vertex pool at `$00C470C8`.

**Edge-builder index convention:** handlers at `$84BA`/`$8512`/`$855E`
read vertex indices from the stream as **byte offsets into the pool**
(`lea (a3,d1.w), a4` with a3=`$00C470C8`).  ÷6 → vertex index.  Same
convention as cluster sub-list indices after `$800C`.

### PP / $70 observations (still open)

| Mesh   | Setup | PP   | Secondary cmds          |
|--------|-------|------|-------------------------|
| 39738  | 4070  | $22  | 4240, 4380              |
| 39800  | 4070  | $20  | 4340, 4500              |
| 398C6  | 4070  | $20  | 4340, 4500              |
| 39E00  | 40B0  | $BA  | 4180, 4480              |
| 39F66  | 40B0  | mixed| different structure    |
| (36xxx)| 4070  | —    | second word is **60xx**, not 40PP |

- Aircraft use `4070 40PP C016 … 00PP C016` (PP restated after each group).
- **$35xxx/$36xxx family** uses `40xx 60PP C1xx` — the secondary word is
  **`$60PP`** (not `$40PP`).  Same PP role, different high nibble.
  Examples: `4058 6058 C162`, `4064 6014 A07A`, `4058 602C C152`.
  Group separator is `$C1xx` (vs aircraft `$C016`).
- Many non-aircraft DLs use `4070 60xx C1xx` instead — different command class.
- The geometry walker only tests **bit 14** of `$4xxx` (skip-next); the low
  byte of `$4070` / `$40PP` is **not** consumed on that path.
- No `cmpi.w #$4070` (or `$40xx`) appears in the live CODE image; classification
  is bit-driven.

**Alternate drawable streams (revised):**

Earlier hypothesis of a runtime “expansion step” that consumes PP is
**not supported** by further analysis:

- No code stores `#$800C` (or `#$FD02`) as an immediate — the
  `$00C37Dxx` content is not assembled opcode-by-opcode at runtime.
- The signature `800C 0042 0048 804E FD02` appears only once in the
  image (no template source elsewhere).
- A **drawable map** at `$00C20B94` pairs walker handlers
  (`$00C1D8A4`, `$00C21618`, …) with stream pointers into `$00C37Cxx` /
  `$00C37Dxx` / `$00C372xx` / `$00C335xx` / cluster addresses.
- These alternate streams are **pre-built DLs** (still contain residual
  `4xxx` setup words, then flat `800C` edge groups). Vertex indices in
  the edge groups are **byte offsets** (÷6 → vertex index into the
  transformed pool at `$00C470C8`), not raw word indices.
- Dual-pointer load sites (`$00C1B7C8`, `$00C1B978`, `$00C1BAF2`) pull
  handler + stream ptrs from a per-drawable record, then `jsr (a2)`.
- Preflight vs inflight differ in this region because different
  overlays / active drawables are resident — not because of a live
  expander.


### 60xx secondary form ($35xxx/$36xxx streams)

Parallel to aircraft `40PP`, these streams encode the group parameter as
`$60PP`:

```
40xx 60PP C1yy …     ; setup, PP secondary, group sep
40zz 60PP C1ww …
```

| Stream | Pattern | PP (lo of 60xx) |
|--------|---------|-----------------|
| `$367C8` | `4058 6058 C162` / `4070 6058 C170` | `$58` |
| `$367F4` | `4058 602C C152` / `4070 602C C160` | `$2C` |
| `$35EEA` | `4064 6014 A07A` / `40B8 6014` | `$14` |
| `$36CDE` | `4880 600E A076` | `$0E` |

Walker still only tests bit14 of `$4xxx`; `$60xx` has bit14 clear so it
is classified as a non-edge, non-skip command and falls through the
threshold path.  PP consumer remains open for both encodings.

**PP remains open:** neither the primary walker nor the jump-table path
consumes the low byte of `$40PP`.  It is most likely an offline /
asset-build parameter, or a state value for a path not active in the
in-flight snapshot.



### $35xxx/$36xxx streams: shared sublists via B000 pointers

These pure-DL streams rarely embed vertices.  Instead they reference
**shared edge sublists** with absolute CODE pointers:

```
… 00B0 0018 60xx B000 00C3 6430 …
                      ^^^^ ^^^^ ^^^^
                      marker  abs ptr → $00C36430
```

`$B000` is a high-word marker; the following long is an absolute address
in the CODE dump.  Shared targets (examples):

| Addr | Role |
|------|------|
| `$00C36430` | edge groups `806C`/`807C`/`8008` + raw indices |
| `$00C36368` | `8054` edge chain |
| `$00C36338` | `7FFF`-framed edge attribute blocks |
| `$00C363E4` | similar edge groups |

Indices in these sublists are **raw vertex numbers** (not ÷6), into the
runtime-transformed pool (`$00C470C8`), not a static table next to the
DL.  After `FFFF FFFF` many streams switch to 2-D outline records
(`0030 0090 2402 …`) — silhouettes, not 3-D verts.

Port implication: without the live vertex pool these streams cannot
yet be turned into stand-alone meshes.  Cluster records remain the
reliable source for static geometry.


### Instance-pool streams (`$00C43xxx`)

Distant-LOD / simple objects use a different record shape than the
aircraft cluster:

```
Variant A (FFFF-separated):
  +0   .w  header (e.g. $0800, $0500)
  +2   .l  $FFFF FFFF separator
  +6   .l  pointer to body
  body: .w N, then N × (x,y,z) int16 triples

Variant B (dual-pointer, e.g. $04D0 / $0C00):
  +0   .w  header
  +2   .l  ptr1 (LOD / secondary record)
  +6   .l  ptr2 (body → N + XYZ)
  +A   …   inline edge cmds (800C / raw indices)
```

Example: `$00C4335E` has N=5 coarse verts (multiples of 64–256).  Edge
cmds use small raw vertex indices (not always byte-offset/6).  Ported as
`mesh_instance_4335E()`.

### Drawable map (`$00C20B94`)

Table of drawable descriptors used when selecting the stream for the
walker.  Records are groups of longwords (handler + stream pointers),
with `$00000000` separators.  Representative entries:

| Handler | Stream(s) | Notes |
|---------|-----------|-------|
| `$00C1D8A4` | `$00C37DCA` / `$00C37DC8` | Walker trampoline + alternate DL |
| `$00C21618` | `$00C37230` / `$00C37228` | Alt handler + stream pair |
| `$00C21618` | `$00C335B0` / `$00C335A8` | |
| `$00C21618` | `$00C3AE70` / `$00C3AE68` | |
| `$00C21672` | `$00C3B518` | |
| `$00C21712` | `$00C3B7BE` | |

Dual-pointer load (`$00C1B7C4`): `move.l (a1)+,a2` (handler),
`move.l (a1)+,a0`, `move.l (a1)+,$4476E`, `move.l (a1)+,$44772`, then
`jsr (a2)`.

**Selection path:**

1. `$00C1B6D4` / `$00C1B6DC`: `lea $00C4D6E2` or `$00C4DD72`, a0 —
   two **scene/drawable lists**.
2. `move.w $00C446E2, d0` / `adda.w d0, a0` — byte index into the list.
3. Record layout (stride **`$18`**):
   ```
   +0  .w  flag (stored to $00C44593; low 4 bits → $00C447F0)
   +2  .l  pointer into object→drawable dispatch table ($00C21000+)
   +6  …   extra words (position/state — not fully decoded)
   ```
4. `move.l (dispatch_ptr), a1`-equivalent: the dispatch slot holds
   `handler, stream, stream, stream` (LOD variants).
5. `cmpi.l #$00C1D8A0, (handler)` — prefer walker-trampoline records.
6. Dual-pointer load + `jsr (a2)`.

Example scene-list entries (`$00C4D6E2`):

| flag | dispatch slot | handler | stream |
|------|---------------|---------|--------|
| `$2D03` | `$00C21064` | `$00C1D8A0` | `$00C39690` |
| `$4602` | `$00C21258` | `$00C1D96C` | `$00C3A330` |
| `$3E02` | `$00C211B8` | `$00C1D8A0` | `$00C367C8` |
| `$5202` | `$00C21348` | `$00C08224` | `$00C4334A` |

Alt list `$00C4DD72` mostly targets handler `$00C08232` (geometry walker
sibling) with instance-pool streams (`$00C43xxx`).

**Scene-list record layout (stride `$18`):**

```
+0   .w  flag
        hi = object-type candidate (2D, 15, 46, 3E, 3F, 52, …)
        lo = LOD tier (01 / 02 / 03) — matches trailing LOD word
+2   .l  dispatch slot ptr → (handler, stream×3)
+6   .w  coord A (often large negative: DC00, EE00, FE00, …)
+8   .w  0000
+A   .w  coord B (E400, 3600, 1949, …)
+C   .w  small param (0010, 0015, 001D, …)
+E   .w  type tag (3E42 / 3F44 most common)
+10  .w  0000
+12  .w  LOD echo (0000 / 0100 / 0200 / 0300)
+14  .w  0000
+16  .w  0000
```

Flag low byte and +12 LOD word agree on the active tier.  Stream
`$00C39690` (flag `$2D03`) is a pure DL with PP=`$72`/`$22` and Axxx
offset tables — sits just before the `$39710` cluster.

**Coord scale (hypothesis):** `coord_a` / `coord_b` are **Q8.8** world
XZ.  Observed range ≈ (−36, −28) … (+41, +56) after ÷256.  The Flight
demo positions scene-loaded meshes with this scale (further ×0.15 for
the demo camera).  Unconfirmed against live object-pool positions
(pool was zero in the inflight snapshot).

**Pre-walker setup handlers** (from the drawable map):

| Addr | Role |
|------|------|
| `$00C1D8A4` | Walker trampoline (`bra` → `$00C1D96C`) |
| `$00C21618` | Object-flag setup (`$00C44EBC` checks) then `jmp $00C1D8A4` |
| `$00C21672` | Similar setup; lea `$00C3B518` stream |

`$00C21618` tests object/instance flags and may write `$000F` into a
field before falling through to the walker — a LOD/visibility gate
rather than a geometry transform.

Cluster catalogue (7FFF-separated records `$00C39710`–`$00C3A980`):

| Addr | Tags | Role |
|------|------|------|
| `$39710` | 0100/0200 | Aircraft (→ 39738 body) |
| `$397D8` | 0100/0200 | Aircraft (39800) |
| `$3989E` | 0100/0200 | Aircraft (398C6) |
| `$39958` | 0100/0200 | Aircraft (39980) |
| `$39B3E` | 0100/0200 | Aircraft / link-graph |
| `$39D92` | 0100/0300 | LOD wrapper → `$39DF6` / 39E00 body |
| `$39F38` | 0100/0300 | Aircraft variant |
| `$3A980` | 0100/0300 | Structure (ring cross-section) |


### Command jump table (`$00C1E840`)

A second classification path (sites `$00C0838E`, `$00C1E468`, …) dispatches
via a **longword jump table** at `$00C1E840`, indexed by
`(cmd & $3FFF)` as a **byte offset**:

```
move.w  (a2)+, d0
… bit15 / $FFFF checks …
move.w  d0, d1
andi.w  #$4000, d1        ; count 4xxx cmds
andi.w  #$3FFF, d0
lea     $00C1E840, a0
move.l  (0,a0,d0.w), a0   ; handler
jsr     (a0)
```

Selected handlers (aligned low bytes only):

| cmd lo | Handler | Role (hypothesis) |
|--------|---------|-------------------|
| `$00`  | `$00C1E978` | end-group / rts |
| `$0C`  | `$00C1EBB4` | begin edges → edge-list `$00C4ACC8` |
| `$18`  | `$00C1F23C` | end + attrs |
| `$20`  | `$00C201E2` | setup + pen from a2 |
| `$70`  | `$00C0C264` | **nop** (`moveq #0; rts`) |
| `$B0`  | `$00C20786` | setup variant |

`$4070` therefore resolves to a **no-op stub**.  Unaligned PP values
(`$22`, `$BA`) are not valid jump-table indices; they only appear as
parameter words skipped by the primary walker, never as dispatch keys.
This reinforces that PP is not meaningful to either of the two runtime
walk/dispatch paths (consumer still unlocated).

**Two dispatch paths summarised:**

| Path | Entry | Classification | Handles |
|------|-------|----------------|---------|
| Primary walker | `$00C1D96C` / `$00C08232` | bit15 / bit14-skip / thresh÷8 | Expanded edge streams (`800C`/`804E`/…), raw DLs (skips PP) |
| Jump-table | `$00C0838E`, `$00C1E468`, … | `(cmd&$3FFF)` byte → `$00C1E840` | Aligned opcodes only (`$00/$0C/$18/$20/$70`/…) |

The jump-table `$0C` handler (`$00C1EBB4`) and siblings at `$00C084BA` /
`$00C08512` / `$00C0855E` **build the edge list** at `$00C4ACC8`
(header, count, XYZ triplets from vertex pool `$00C470C8`) — the same
structure consumed by the DDA body (`$00C231F8`).  Expanded-stream
opcodes with unaligned low bytes (`$4E`, `$12`, `$2A`) are processed by
the primary walker, not the jump table.

## Pointer table / LOD slots

| Abs (load model) | Live | Role |
|------------------|------|------|
| `$5400` | `$00C4C4C8` | longword pointer table to **2-D hull starts** |
| | `$00C4C4F4` | companion records (stride 256) |

In-flight slots point into the **model cluster** `$00C39700`–`$00C3A600`.
Pointers land on the **hull pairs**, not the `FFFF`/`N` vertex header:

```
slot → (x,y).w pairs…  FFFF FFFF  N  (x,y,z).w…  display list…
```

`$00C1CD38` reads the mesh pointer as hull start, transforms coordinates,
and writes projected pairs into the companion record (`$00C1CD88`).

### Slot selection (LOD / proximity, not object-type)

```
; proximity gate — threshold word $00C446DE/$00C446E0 = 320
; positions asr.l #8, then |Δ| per axis; reject if any |Δ| >= 320

; LOD countdown path ($00C2548C)
moveq   #8, d5
loop:   … re-test proximity …
        subq.w  #1, d5
        bgt     loop

; slot lookup ($00C25372)
cmpi.w  #10, d5            ; valid 0..9
bge     reject
addq.w  #1, d5             ; path-C starts d5=-1 → slot 0
lea     $00C4C4C8, a0      ; mesh pointer table
lea     $00C4C4F4, a1      ; companion base
move.w  d5, d0
asl.w   #2, d0             ; ×4 → pointer index
move.l  (0,a0,d0.w), d1    ; mesh header (blt if empty)
move.l  d1, a0
asl.w   #6, d0             ; d0 = d5×256
adda.w  d0, a1             ; companion record
addq.w  #2, a1
; match object+0x30 / +0x32 against companion bytes ±1
```

Object `+$10` / `+$26` are behavioural flags; the **mesh slot is
distance-selected**.

### Companion records (stride 256)

```
record = $00C4C4F4 + slot×256 + 2
```

First two bytes are compared to `object+0x30` and `+0x32` with
tolerance ±1; mismatch rejects the slot.

**Runtime-populated** by `$00C1CD38`–`$00C1CD88`:

```
; d2/d3/d4 = translation, d7 = scale shift (0..15)
andi.w  #$F, d7
asl.l   d7, d2 / d3 / d4     ; scale
andi.w  #$3FFF, d2 / d4      ; 14-bit mask
; per coordinate:
move.w  (a0)+, d6
add.w   d2, d6               ; translate (d4 for other axis)
move.w  d6, (a1)+            ; write into companion
```

Live record layout:

```
[2 match bytes] [translated pairs …] FFFF FFFF [more pairs …]
```

Companion stores **scaled+translated** 2-D coordinates derived from the
mesh hull, not raw copies.

### Observed live slots

| Slot | Hull start | FFFF/verts | Band |
|------|------------|------------|------|
| 0–2 | `$00C3A58C` (structure, 5 pairs, N=4) | `$00C3A5A0` | near |
| 3–8 | `$00C39B52` (aircraft, 5 pairs, N=5) | `$00C39B66` | mid–far |
| 9+ | `$00000000` | — | empty |

Proximity threshold: **320** after `asr.l #8` (`$00C446DE` / `$00C446E0`).

## Cluster record layout

Each mesh in `$00C39700`–`$00C3A600` is bounded by `$7FFF $7FFF` separators:

```
7FFF 7FFF                 ; record separator / end of previous
tag0 tag1                 ; type tags (aircraft: 0100 0200; some 0100 0300)
0000 × N                  ; reserved padding
(x,y).w pairs…            ; 2-D top-down hull / radar silhouette
FFFF FFFF                 ; end of hull
word N                    ; declared vertex count (often under-states)
(x,y,z).w × N+            ; signed local coords — continues past N
display list…             ; edge/poly commands ($40xx/$80xx/$C0xx)
7FFF 7FFF                 ; end of record
```

Aircraft hulls typically have **6 pairs** forming a closed top-down
silhouette (nose / wings / tail).  Tags `0100 0200` mark the standard
aircraft family; `0100 0300` appears on larger/long-nose variants.

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

1. **Top level** — high-byte command taxonomy (low byte = parameter):

   | High byte | Role (hypothesis) |
   |-----------|-------------------|
   | `$40` | setup / begin (low=`$70` common on aircraft) |
   | `$41`–`$45` | group / face bank |
   | `$C0` | separator / end-group (`$C016` very common; also `$C01C`, `$C00A`) |
   | `$A0`–`$A2` | secondary command or local pointer |
   | `$80`–`$83` | edge/sub-list opener (also used inside sub-lists) |

   Groups often look like `40xx pppp C016` or `4xxx pppp C016` where
   `pppp` is a shared parameter (e.g. `$0022` / `$0020` / `$00BA` repeated
   across groups of one mesh — scale, colour, or flags).

   After the state block comes a short table of **byte offsets** relative
   to the mesh header (`$0270`, `$02D6`, …).

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
   optional pre-header hull), ended by `$7FFF $7FFF`.

`mesh_edges_from_dl()` in the port implements the sub-list decoder and
recovers a useful subset of edges.  It always scans the supplied DL for
inline `C00C`/`800C` groups and, when an offset table or link-graph is
present, also follows those targets.  Hand-authored edges remain the
primary draw path until top-level parameters are fully mapped.

Some models (e.g. `$00C39B66`, `$00C39980`) use a different top-level mix
(`C0C0`, `414A`, …); the sub-list format still applies where offsets are
present.

**Shared sub-lists:** several aircraft DLs (notably `$00C39738`) have
offset-table entries that land *later* in the cluster, inside the body
of neighbouring records such as `$00C39980`.  Those targets contain
`C00C … 0030 0036` groups whose indices (8, 9) are valid only for the
larger 10-vert meshes.  The parser therefore filters by the caller’s
`nverts`; high indices simply drop for the 8-vert models.

## Model cluster catalogue

| Address | Declared N | Usable verts | Notes |
|---------|------------|--------------|-------|
| `$00C39738` | 5 | **10** | aircraft cross ±500 |
| `$00C39800` | 5 | **10** | aircraft cross ±437/525 |
| `$00C398C6` | 4 | **8** | smaller craft |
| `$00C39980` | 4 | **8** | C0C0-style DL; dump-derived edges + pens |
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

## Radar scope pair-walker (not mesh hull)

**Correction:** `$00C2FF10` walks a **fixed overlay** at `$00C4D454`
(screen-relative pairs with bit15 flags, e.g. `$809E,$00A6`), **not**
mesh 2-D hulls.  Centre `(158,167)`, X clip 319, plotters `$00C2E14C` /
`$00C2E162` are real radar-scope primitives, but the data source is the
reticule/marker list.

The runtime consumer of mesh hull pairs remains **open**.

## Display-list sub-lists (refined)

Top-level DL ends with a short **offset table** (e.g. `0270 02D6 02E2
0330 035C`) then trailing data / `7FFF`.

**Offset base = display-list start** (first opcode word).  Byte offsets
added to the DL address resolve to sub-list bodies (verified 5/5, 4/4,
3/3 on aircraft meshes 39738 / 39800 / 398C6).

```
800C 0006 0024 002A 8000 …   ; triangle verts 1,6,7 (byte-off÷6)
800C 000C 0012 001E 8018 …   ; triangle verts 2,3,5
C00C 0006 0030 0036 8024 …   ; polyline variant
```

| Cmd | Role |
|-----|------|
| `$800C` | **begin** edge/poly; following words = byte-offsets (÷6 → vert) |
| `$8000` | **end** group (no attributes) |
| `$8018`/`$801E`/`$8024`/`$802A` | **end** group with attribute word(s) following (e.g. `$1007`, `$0C07`, `$0808`) |
| `$C00C` | polyline / alternate edge class (same vert encoding) |

`$800C` is typically followed by 2–3 vert indices (line or triangle).
End-with-attrs forms carry a packed attribute word:

| Field | Observation |
|-------|-------------|
| hi byte | often multiple of 4 (0,4,8,12,16,24,28); `hi/4` ∈ 0..7 — **colour index** candidate |
| lo byte | common values 3,7,8,10,11,13 — **style / flags** candidate |
| `$100B` | most frequent (colour 4, style 11) |
| `$FC00` | special (appears with `$8024`) |

`mesh_edges_from_dl()` uses DL-start as the offset base.

## Top-level DL structure (aircraft)

```
4070              ; setup (lo=$70 common across aircraft)
40PP              ; set group parameter PP
C016              ; separator
42/43/45xx        ; secondary command (lo often $40/$00/$80)
00PP              ; restate group param
C016
43/45xx 00PP
Axxx              ; pointer (see below)
80LL              ; end marker: DL_start+(80LL&$FFF) → trailing/7FFF
Axxx
[offset table…]   ; byte offsets from DL start → edge sub-lists
[trailing 2D…]
7FFF
```

| Mesh | PP | 80LL end-off | Verified |
|------|-----|--------------|----------|
| 39738 | `$22` | `$5C` | `$3977A+$5C` → `0072 7FFF` |
| 39800 | `$20` | `$5A` | `$39842+$5A` → `0070 7FFF` |
| 398C6 | `$20` | `$5A` | same pattern |
| 39E00 | `$BA` | `$F4` | larger model |

**`80LL`**: low 12 bits = byte offset from DL start to trailing section.

**`Axxx`**: absolute pointers into a **runtime mesh-instance area**
(`$00C43700`–`$00C43B00`).  For the three standard aircraft,
`DL_start + Axxx` → `$00C439BE` (inside instance `$00C439B2`).

Runtime instance pool — **fixed stride 106 bytes (`$6A`)**:

```
+0   04D0                 ; type magic (shared by all 11 instances)
+2   ptr A = base+46      ; sub-header (0C00 FFFF FFFF …)
+6   ptr B = base+78      ; vertex block (N, coords… FFFF)
+10  8078 000C …          ; edge/attr commands (same 8xxx encoding)
+46  [ptr A target]
+78  [ptr B target: N + view-space coords]
+106 [next instance]
```

Self-contained drawable: internal ptrs always land at fixed offsets.
Edge section matches static DL encoding; +78 holds coordinate pairs
(N + fixed-point templates, not screen pixels).

**Static, not runtime-built:** pool is identical in preflight and
inflight dumps — pre-built compact mesh templates (LOD / 2-D stand-ins),
not per-frame expansions of the 3-D cluster meshes.

### Object → drawable dispatch table (`$00C21000`–`$00C21500`)

Groups of 3 identical pointers (+ optional NULL/handler) map object
types to drawables:

| Target class | Examples | Role |
|--------------|----------|------|
| 3-D cluster DL | `$00C3977A`, `$00C39842`, `$00C398FC`, `$00C39E42`, `$00C39BA8`, `$00C3A4DE` | Full mesh DLs |
| LOD table | `$00C3A47E`, `$00C3A576` | Hierarchical LOD (below) |
| Instance pool | `$00C43772`…`$00C43FCC` (**19 entries**) | Compact templates |
| Walker/handler | `$00C1D8A0` → `$00C1D96C`, `$00C08232` | Geometry walkers |

`$00C1D8A0` is a trampoline (`bra.w $00C1D96C`).  Most instances use
ptr offsets +46/+78; a few use +64/+104.

### LOD tables (hierarchical)

Some dispatch entries point to LOD headers, not flat DLs:

```
0003                 ; level count (ptrs = count+1)
0000 0000            ; flags / padding
ptr0 ptr1 ptr2 ptr3  ; absolute ptrs into mesh body
(x,y).w pairs…       ; 2-D hull
FFFF FFFF
N  (x,y,z)…  DL…    ; full mesh follows
```

Sub-pointers land mid-record (vert groups or partial DLs) for
distance-selected detail.  Examples: `$00C3A47E`, `$00C3A576`.

**`PP` / `$70`**: meaning still open.  Tags do not correlate with PP.
Immediates `$70`/`$BA`/`$20` in code are range bounds / radar scroll,
not DL parameter loads.  Same values on the ADF (static, not patched).

Walker threshold `(w & $3FFF) / 8` vs d1 (`$00C44878` ≈ 361): all
observed PP values pass (`$BA/8 = 23 ≪ 361`).  For `$4xxx` this walker
only tests **bit 14** (skip-next); the low byte is not consumed here.

### Geometry walker command classification (`$00C1D9AC` / `$00C08232`)

```
move.w  (a2)+, d0
blt     high_bit          ; bit15 → $8xxx/$Axxx/$Cxxx edge cmds
move.w  d0, d2
andi.w  #$3FFF, d0
asr.w   #3, d0            ; (word & $3FFF) / 8
cmp.w   d1, d0            ; threshold filter
bgt     skip_entry
andi.w  #$4000, d2        ; bit14?
beq     plain_offset      ; clear → add word to a2 (in-list jump)
addq.w  #2, a2            ; set ($4xxx) → skip next word
bra     loop
```

`$4xxx` is recognized (bit 14) and skips the following word — consistent
with command+param encoding, but the low-byte consumer is not this path.

**Worked example** — aircraft DL `4070 4022 C016 4240 0022 C016`:

| Fetch | bit15 | bit14 | thresh | Action |
|-------|-------|-------|--------|--------|
| 4070  | 0     | 1     | 14     | skip next word (`4022`), loop |
| C016  | 1     | —     | —      | edge-command path |
| 4240  | 0     | 1     | 72     | skip next word (`0022`), loop |
| C016  | 1     | —     | —      | edge-command path |

The PP words (`4022`, `0022`) are **never fetched as commands** by this
walker; they are parameter payloads attached to the preceding `$4xxx`.
The same skip occurs for non-aircraft `4070 60xx` pairs.  A second
occurrence of the bit14-skip pattern exists at `$00C1EC2C` (related
draw path).  The actual PP consumer remains open.

## Open

- [~] Display-list edge encoding + offset base + `80LL` end-offset known;
      `PP`/`$70` still open
- [x] `Axxx` → instance pool (stride `$6A`, tag `$04D0`, ptrs at +46/+78)
- [x] Handler `$00C08232` = geometry walker (same `link a6,#-152` as `$00C1D96C`)
- [x] Instance pool is **static** (preflight ≡ inflight); 19 entries
- [x] Object→drawable table `$00C21000`–`$00C21500` (3D DLs / LOD tables / instances / walkers)
- [x] LOD table format: count + ptrs into mesh body + hull + FFFF/N/verts/DL
- [x] `$00C1D8A0` = trampoline to `$00C1D96C`
- [x] `$5400` slot index = LOD/proximity counter `d5` (0..9), not object type
- [x] Proximity threshold = 320 after asr#8 (`$00C446DE`); countdown d5=8..1
- [x] Companion records: stride 256, first 2 bytes match object+0x30/+0x32 ±1
- [x] Companion records runtime-written at `$00C1CD88`; match-bytes + 2-D pairs
- [x] `$5400` pointers = **hull starts**; `$00C1CD38` transforms hull → companion
- [x] Full cluster record layout (`7FFF` separators, tags, hull, verts, DL)
- [~] `$00C2FF10` is radar scope but consumes fixed overlay, not mesh hull
- [x] Offset-table base = DL start; `80LL` = end-of-DL byte offset
- [x] Edge cmds: `$800C` begin+verts, `$8000` end, `$8018`/`$1E`/`$24`/`$2A` end+attrs
- [x] Link-graph DL style (39B66): `$80xx`/`$C0xx` with lo ≥ `$30` = intra-DL
      byte-offset link to sub-list (e.g. `$8070` → DL+$70 → `$C00C` edges)
- [~] Attribute word: hi/4 = colour candidate; **unconfirmed** as pen source.
      `$100B` appears 12× in 3D cluster, **0×** in instance pool (instances use
      simpler edge encoding without these attrs).
- [x] Pen set from **separate draw-command stream** (`move.w (a2)+, $00C4468C`);
      `$5ee` copies pen → `$326` then draws.  Mesh edge attrs not in this path.
- [ ] Exact meaning of group param PP (`$22`/`$20`/`$BA`) and setup `$70`
- [x] Companion transform: scale (asl d7) + translate (d2/d4) + 14-bit mask
- [x] Walker classification: bit15=edge, bit14=skip-next, (w&$3FFF)/8=threshold

## Port

`src/mesh.hpp`:

- `Mesh` holds verts, edges, **hull**, tags, raw display_list, edge_pens
- `mesh_parse()` — full cluster record (7FFF / tags / hull / verts / DL)
- `mesh_edges_from_dl()` — offset-table (`Axxx 80LL Axxx`) + link-graph
  (`$80xx` lo≥$30) + inline `$800C` edge decoder; attr_hi/4 pens
- `mesh_from_words()` — parse + auto edges
- `mesh_from_dump_file()` — load record from inflight CODE dump by live addr
- `SceneDrawableRecord` / `scene_list_from_dump()` — scene list A/B loader
- `DispatchSlot` / `dispatch_slot_from_dump()` — handler + stream×3 from $21000
- `cluster_record_start()` — map stream addr to enclosing 7FFF record
- `mesh_from_instance_dump()` — instance-pool ($43xxx) N+XYZ loader
- `mesh_from_lod_dump()` — LOD table (count+ptrs+hull+N+XYZ+DL)
- Auto-load skips pure DL streams ($35/$36/$39690) — no vertex header; would
  parse opcodes as coords and draw long garbage lines.  Instance verts ÷64.
- `mesh_aircraft_39738/39800/…` — hand-authored with dump-derived edges
- `draw_mesh()` — 3D wireframe; `draw_hull_2d()` — top-down silhouette

Dumps are copied next to the binary at build time (`dumps/inflight/…`).
