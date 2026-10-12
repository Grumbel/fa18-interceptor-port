# Mesh / model format (in-flight)

## Pointer table / LOD slots

| Abs (load model) | Live | Role |
|------------------|------|------|
| `$5400` | `$00C4C4C8` | longword pointer table to mesh headers |
| | `$00C4C4F4` | companion records (stride 256) |

In-flight slots point into the **model cluster** `$00C39700`–`$00C3A600`.

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
tolerance ±1; mismatch rejects the slot.  Rest of the record holds
packed attribute / 2-D data (`FFFF` terminators visible).

### Observed live slots

| Slot | Mesh target | Band |
|------|-------------|------|
| 0–2 | `$00C3A58C` (structure A) | near |
| 3–8 | `$00C39B52`/`$00C39B66` (aircraft B ±320) | mid–far |
| 9+ | `$00000000` | empty |

Proximity threshold: **320** after `asr.l #8` (`$00C446DE` / `$00C446E0`).

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

## Open

- [~] Display-list → edge encoding: **sub-list byte-offset/6 decoded**;
      top-level high-byte taxonomy sketched, low-byte params still open
- [x] `$5400` slot index = LOD/proximity counter `d5` (0..9), not object type
- [x] Proximity threshold = 320 after asr#8 (`$00C446DE`); countdown d5=8..1
- [x] Companion records: stride 256, first 2 bytes match object+0x30/+0x32 ±1
- [ ] Remaining companion-record bytes (attribute / 2-D payload)
- [ ] Whether 2D hull pairs are used at runtime or only for tools
- [ ] Full face/poly strip interpretation (cmd word low-nibble = count?)
- [ ] Meaning of repeated group parameter (`$0022` / `$0020` / `$00BA`)

## Port

`src/mesh.hpp`:

- `mesh_parse()` — generic header/vertex/DL extractor with opcode heuristic
- `mesh_edges_from_dl()` — partial decoder (sub-list byte-offset indices)
- `mesh_aircraft_39738/39800/398C6/39E00/39F66`
- `mesh_from_dump_A/B`, `mesh_structure_3A4A8`
- `draw_mesh()` — hand-authored edges (primary); DL decoder supplements
- Each `Mesh` retains raw `display_list` for future decoder work
