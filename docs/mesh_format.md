# Mesh / model format (in-flight)

## Pipeline overview

```
Object type
    │
    ▼
$00C21000 dispatch table ──► 3D cluster DL  ($00C39700+)
    │                        LOD table       (count+ptrs+hull+mesh)
    │                        Instance pool   ($00C43700+, stride $6A)
    │                        Walker trampoline ($00C1D8A0 → $00C1D96C)
    │
    ▼
LOD/proximity ($00C4C4C8 slot table, d5 countdown, threshold 320)
    │
    ├─► Hull → companion ($00C1CD38 transform, stride 256)
    │
    └─► Display list walk ($00C1D96C / $00C08232)
            bit15 → edge cmds ($800C verts, $8018+attrs, $80LL end)
            bit14 → $4xxx skip-next (PP low byte not consumed here)
            else  → threshold filter / in-list offset
            │
            ▼
        Pen from separate a2 stream → $5ee line draw
```

**Known gaps:** PP (`$22`/`$20`/`$BA`) / `$70` low-byte consumer;
edge-attr semantics beyond residual style/flags hypothesis.

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
recovers a useful subset of edges.  Hand-authored edges remain the
primary draw path until top-level parameters are fully mapped.

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

- `Mesh` holds verts, edges, **hull**, tags, raw display_list
- `mesh_parse()` — full cluster record (7FFF / tags / hull / verts / DL)
- `mesh_edges_from_dl()` — offset-table (`Axxx 80LL Axxx`) + link-graph
  (`$80xx` lo≥$30) + inline `$800C` edge decoder; attr_hi/4 pens
- `mesh_aircraft_39738/39800/…` — include hull pairs + tags from dump
- `draw_mesh()` — 3D wireframe; `draw_hull_2d()` — top-down silhouette
