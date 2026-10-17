# Camera and object handling

Source of truth: native reimplementations in `src/native/scenedraw.cpp`,
`src/native/flight.cpp`, and call sites in `flow.cpp` / `render.cpp`.

## Two parallel object pools

| Pool | Base | Stride | Count | Role |
|------|------|--------|------:|------|
| **Scenario** | `$5b47c` | **512** | 16 | Aircraft, ships, large entities |
| **Record** | `$5d47c` | **32** | 16 | Smaller / secondary entities |

Player / focused scenario index: word **`$5abd6`** (byte offset into the
scenario pool: `base + $5abd6`).

Dirty flag: bit 4 of byte at `slot+1` (set for all slots by `$2f9da`).

## Scenario record fields (confirmed uses)

| Offset | Size | Use |
|--------|------|-----|
| +0 | word | flags (bit15 tested before draw; negative → skip) |
| +1 | byte | flags (bit4 = dirty) |
| +6 / +8 | word | grid / sector coords (LOD with `$5ac44`/`$5ac46`) |
| +10 | byte | related type/flags (`$5ab49`) |
| +12 / +14 | word | contributions to expanded list position |
| +16 | long | height-like term (added to `$5ad70` for LOD) |
| +20 / +24 / +28 | long | **world position X / Y / Z** (X,Z often `& $3FFFFF`) |
| +98 | byte | seat / view mode (cockpit eye offset select) |
| +$66 / $68 / $6A | word | attitude angles (table units) |
| +$80 / +$92 | 9×word | body matrices |
| +146..162 | 9×word | basis used to rotate cockpit eye offset |

## Camera position

Two entry points feed **`$5ad5a` / `$5ad5e` / `$5ad62`** (stored camera XYZ).

### `$2f4a6` — high-level (game_frame path)

- **External** (`$5aa7d ≠ 0`):  
  `call $2f220`; then `$5ad74..$5ad7c ← $5af36/$3a/$3e` (eye from
  external tracker); `$5ad5*` already set by `$2f220`.
- **Cockpit** (`$5aa7d == 0`):  
  Eye offset from seat mode at player+98, rotated by words at +146..162  
  (`mulsw`, `asr #6`), added to player +20/24/28 → `$5ad74..`.  
  `$5ad5* = −eye` (with X/Z of player contrib masked `$3FFFFF`).

Scaled words `$5ad6a/$6c/$6e` = `$5ad5* >> 8`; `$5ad70` = Y component.

### `$2f220` — external camera detail

Uses player scenario pos (+20/24/28) and offsets **`$5af2a/$2e/$32`**:

1. Δ = (player_pos + `$5af2a..`) with sign bits in d0; `asr #8`.
2. Hypot `$308cc` → distance in d1.
3. If distance out of band → zero delta and fall through.
4. Else scale delta by distance factors; optionally subtract player pos
   again so result is a **chase offset** in world space.
5. Final: `$5ad5* = delta + $5af2a..` (or zero path + `$5af2a..`).

`$5af36` / `$5af2a` pairs are written by world-origin helpers
(`flow.cpp` `$2d3cc` family): positive set and negated set of the same
vector.

## Per-frame scene list

### `$3127c` — expand + depth sort

Builds a sorted draw list (24-byte entries) into buffers around
`$5d684` / `$649ba`, using scenario and record pools, LOD from
`$5adb0` / distance, and depth keys.  Outputs index words at `$63b18`
style tables for the draw pass.

### `$2fc14` — draw

1. **Scenario pass** over list `$63c7a` (index word `$5aca2` / `$5aca8`).
2. **Sorted pass** over `$649ba` until word `$FFFF`.

For each object:

| Scratch | Meaning |
|---------|---------|
| `$5ae28/$2c/$30` | Object position (world / LOD-scaled) for this draw |
| `$5ae22/$24/$26` | Same `asr #8` (word form) |
| `$5ae34` | Height residual vs `$5ad5e` |
| `$5ae38` | Distance from hypot `$308cc` |
| `$5acac` | Type index (hi byte of list word) |
| `$5adb0` | LOD / scale byte |

Dispatch: `a2 = *($36094 + 20 * type_index)` then `jsr (a2)`.

`$36094` is a **type → draw-function** table (20 bytes per entry:
code pointer plus related words).

## Object vs camera in the mesh path

Draw routines (e.g. in `render.cpp` / `ops.cpp`) combine:

- object position `$5ae28..` (or `$5ae22..`),
- camera / eye `$5ad5*` or `$5ad74`,
- view matrix `$5aed0` via `rot_out`.

Exact subtract order is routine-specific (some use `$5ad74` eye,
some `$5ae22` as object origin in view).  Capture for the remaster
scene layer records `$5ae28` / `$5ae22` / `$5ae38` / type at dispatch
(`scenedraw.cpp` `capture_object`).

## Call order (game_frame, simplified)

```
$2112c  game_frame
  …
  $2f4a6  camera_position          → $5ad5*, $5ad74*
  $2f596  view_change_flags
  …
  $2f7b8  scene_update
            $3127c  expand + sort   → $649ba / related
  …
  $2fc14  draw_scene
            scenario list $63c7a
            sorted list $649ba
            per object → $36094 dispatch
```

Matrix build for `$5aed0` is separate (see `docs/horizon.md`): external
tilt/pan or player angles, not inside `$2fc14`.

## Type table `$36094`

Runtime-filled (static image is zeros).  **20 bytes per type index**:

| Off | Size | Loaded as |
|-----|------|-----------|
| +0 | long | draw function → `a2`, then `jsr (a2)` |
| +4 | long | → `a0` (mesh / aux base) |
| +8 | long | → `$5ad2e` |
| +12 | long | → `$5ad32` |
| +16 | long | (remainder of stride) |

Index arithmetic: `offset = type * 20` (`type<<2 + type<<4`).

`$5acac` holds the type index at dispatch (hi byte of the list word for
sorted entries).

## Sorted list `$649ba` entry

Walked with word index `$5aca2` (reset from `$5aca8` after scenario pass).
Terminator: word **`$FFFF`**.

| Off | Size | Role |
|-----|------|------|
| +0 | word | packed: type in **high byte**, flags/LOD in low (low nibble → `$5adb0`) |
| +2 | long | object pointer |
| +6 | 3×word | seeds `d2/d3/d4` before projection |
| +12 | long | → `$5ac2a` |

Comment in port: 24-byte stride for sort buffer copies in `$3127c`.

Scenario list `$63c7a` is a denser index list (word index `$5aca2`, +2 per
entry); each entry leads to a scenario slot and the same `$30872` / type
dispatch path.

## `$30872` — camera-relative distance step

Before type dispatch, both passes call `$30872` then rely on `$308cc`.

```
d1 = $5ad6a >> $5adb0          ; camera X word, LOD shift
d6 = $5ad70 >> $5adb0          ; camera Y (long path)
d7 = $5ad6e >> $5adb0          ; camera Z word
d2 = |d2 + d1|
if $5abb3:
  d3 = |$5ae34 >> 8|           ; precomputed height residual
else:
  d3 = |ext(d3) + d6|
d4 = |d4 + d7|
if |d3| huge → distance = 32767
else → $308cc hypot(d2,d3,d4) → d1, $5ae38
```

So `$5ae38` is an approximate **camera-relative distance** used for LOD
and sort keys.  `$5abb3` selects whether Y already includes camera
(`$5ae34` path from scenario setup).

## `$308cc` — table hypot

Inputs: non-negative words in d2,d3,d4.  Uses table `$30930` twice
(ratio → table → multiply chain).  Output d1 clamped to 32767, stored in
`$5ae38`.

## World-origin vectors `$5af36` / `$5af2a`

`$2d3cc` / `$2d3e0` / `$2d3f4` / `$2d408` load fixed mission origins into
d0/d1/d2, then `$2d41a` stores:

```
$5af36/$3a/$3e = (d0,d1,d2)           ; positive
$5af2a/$2e/$32 = −(d0,d1,d2)          ; X/Z masked with $3FFFFF before neg
```

External camera (`$2f220`) and `$2f4a6` external branch consume these as
chase/target offsets relative to the player scenario position.

## Mesh vertex path (object → view)

Typical full-detail path in `render.cpp` (e.g. around `$329e2`):

```
; local vertex words in d2/d3/d4 (or loaded from mesh)
d0/d1/d2  += object position   ; $5ae28 (X), $5ae30 (Z) — Y path varies
d0/d1/d2  >>= LOD shift
rot_out with matrix at $5aed0  ; 3×3 mulsw, asr #8, write 3 words to (a3)+
```

Distant / simplified path (`$32f6e`):

```
word_x += $5ae22
word_z += $5ae26
row_xz through $5aed0          ; skips full Y row
```

So object position is applied **before** the view matrix multiply.  Camera
orientation is only in `$5aed0` (and related `$5af18` for some HUD paths).
Camera translation is already baked into `$5ae28` / `$5ae22` by the
`$30872` / list-setup stage (object − camera, LOD-scaled).

### `rot_out`

Three rows: each `d2*m0 + d3*m1 + d4*m2`, `asr #8`, store word to `(a3)+`.
Matrix pointer in `a0`/`a1`/`a2` depending on caller; usual view matrix
`$5aed0`.

## `$310c0` — mesh expand into slot tables

Consumes sorted 24-byte entries (same layout as `$649ba`):

```
word flags/type, long obj*, 3×word pos, long aux
```

For each non-skipped entry, resolves mesh data through object-linked
pointers, applies LOD shifts to the 3 words, and writes mesh bases into
tables at `$62a84` / `$62ab0` / related.  Also stores a **pointer into
`$36094`** (type record) into the expansion buffer (`wr32 (a2), a1` with
`a1 = $36094 + type*20`).

So `$36094` is not rewritten each frame; expand stores *pointers to*
type slots for later draw.  The table contents themselves are installed
when meshes/types are loaded (overlay / load path — not in the static
hunk zeros).

## Object position vs camera (summary)

| Stage | What |
|-------|------|
| List setup / `$30872` | Object world pos combined with camera (`$5ad6*` / `$5ad5e`) → `$5ae28` / `$5ae22`, distance `$5ae38` |
| Mesh verts | local + `$5ae28`/`$5ae22` |
| `rot_out` | × `$5aed0` → view space |
| `project_screen_body` | view → screen |

## Scenario draw list `$63c7a`

Built in `$310c0` / expand path when `$5ab5c ≠ 0` and `$5ab5e == 0`:

```
$63c7a ← empty ($FFFF)
for i in 0..15:
  f = scenario[i].byte1
  if (f & 0x40) and (f & 4):
    append word i
    append $FFFF as running terminator
```

Draw (`$2fc14`) walks with index `$5aca2` from 0, steps +2, until
`$FFFF`.  After that pass, `$5aca2 ← $5aca8` and the sorted `$649ba`
pass begins (24-byte stride).

`$5aca4` / `$5aca6` / `$5aca8` are alternate list bases/indices set by
`flow.cpp` when switching list modes.

## More scenario fields (from ops / draw)

| Offset | Use |
|--------|-----|
| +1 | flags: bit6 (`$40`) + bit2 (`$4`) → on `$63c7a`; bit4 dirty |
| +20/24/28 | world position |
| +61 | byte: LOD / priority bias (`op_35884`) |
| +62/66/70 | longs: direction or aim vector (dot with eye-relative pos) |
| +78 | word triple store (lattice intermediate) |
| +110 | word: if ≥ 256, vectors at +62 are not `asr #8` |
| +164 | base for subdivision ops (`op_352c4` / `$353ba`) |

## Eye-relative geometry outside the mesh path

`$5ad74/$78/$7c` (world eye from `$2f4a6`) is subtracted from object
world pos in targeting / visibility code (`op_35884`):

```
d3/d4/d5 = obj(+20/24/28) − $5ad74/$78/$7c
optional asr #8
dot with obj(+62/66/70) → front/back tests, range tables at $650c4
```

So **draw** uses `$5ae28` (camera-relative, LOD-scaled) while **logic**
often uses `$5ad74` (true eye) against raw scenario positions.

## `$30064` scene_build

Chooses section/table/bitmap bases from view flags (`$5ab5c`, `$5aa7e`,
altitude `$5ad5e`), seeds angle words from `$5ac44/$46` or `$5ac40/$42`,
clears sort buffer `$5d684`, then walks a section script to emit entries
that `$3127c` depth-sorts into `$649ba`.

## Index words: `$5abd6` vs `$5acae`

Both are **byte offsets** into `$5b47c` (not slot indices × 512).

| Symbol | Typical role |
|--------|----------------|
| `$5abd6` | Player / camera subject (cockpit eye, matrix build, free-flight) |
| `$5acae` | Focus object for ops, targeting, some render helpers (`obj_5b47c()`) |

They often match for the player aircraft but diverge when another
scenario entity is the ops focus.

## Record pool `$5d47c` (32 bytes × 16)

Compact entities.  Visibility load (`$31b22`):

| Off | Size | Use |
|-----|------|-----|
| +1 | byte | flags (bit4 dirty, same convention) |
| +12 | word | X-like (ext to long) |
| +14 | word | Z-like |
| +16 | long | Y-like |

Index into pool from sorted-list high byte: `record_base + (word & $FF00) >> 3`
(`$31b04`).  Scenario index uses `(word & $FF00) << 1` (`$31b14`).

## `$31494` — visibility / occlusion on `$649ba`

Called from `$2f7b8` after expand.  List entries gain state bits at +2:

| Bit | Meaning |
|-----|---------|
| `$4000` | hidden by another object |
| `$8000` | in front |

Uses plane/polygon tests between nearest drawn entries and the rest, then
compacts; working copy at `$5d684`.

## Position tweak `$3ab16`

Perspective-style correction on a scenario object at `a1`:

```
if (d6 >> 8) == 0: return
adjust a1+20 and a1+28 from d5/d7 and a1+24
clear a1+24
```

Related table form `$3b462` on `$5af6a + (index << 6)`.

## Scene update call order (refined)

```
$2f7b8  scene_update
  $30064  scene_build          ; section script → raw entries
  $3127c  expand + depth sort    ; → $649ba, mesh expand $310c0
  $31494  visibility            ; hide/sort flags on $649ba
  …
$2fc14  draw_scene             ; $63c7a then $649ba
```

## `$5acae` during draw

In the `$63c7a` scenario pass (`$2fc14`):

```
index = word at $63c7a[$5aca2]
$5acac = index                    ; type-ish / slot index
$5acae = index * 512              ; byte offset into $5b47c
a0 = $5b47c + $5acae
```

Sorted pass: `$5acae` from high byte of the packed list word (same
offset encoding).  When `$5acae == $5abd6` and cockpit view, distance is
forced to 32767 so the player body is not drawn as a mesh in the cockpit.

## `game_frame` `$2112c` (in_flight) — camera / objects

```
poll_input / run_game_state
$241f0  view toggles (external/cockpit keys)
$2f596  view_change_flags
$4217c  (pre-camera; view/matrix related — still ASM)
$2f4a6  camera_position → $5ad5*, $5ad74*
$39a54  heading_octant
$243fa  heading alerts
$2f7b8  scene_update
          $30064 build
          $3127c expand + sort
          $31494 visibility
$42f48  horizon dispatcher
$3f284  terrain (when enabled)
$43306  (post-horizon)
…
$2fc14  draw_scene ($63c7a then $649ba)
$272e4  (ordering vs draw depends on $3add8)
… cockpit instrument block if $5aa7d==0 …
```

Camera and scene lists are finalized **before** `$2fc14` draws.  Horizon
/ terrain (`$42f48` / `$3f284`) sit between list build and object draw.

## World origin presets

Mission helpers `$2d3cc`…`$2d408` install fixed `(X,Y,Z)` into `$5af36`
and the negated pair `$5af2a`.  External chase camera is expressed relative
to those origins plus the player scenario position.

## `$4217c` — view matrix / look setup (before camera)

Called every in-flight frame **before** `$2f4a6`.

```
if $5aa7d != 0:   ; external
  $4219a
else:             ; cockpit
  $422f8
```

### External `$4219a`

1. Resolve scenario base (`$5b47c+$5abd6`, with fallbacks via `$5acba`).
2. Seat mode at +98 selects local offset `(d3,d4,d5)` (e.g. 0,0,1 or 0,0,−4).
3. **`$2d4a0`**: rotate offset by matrix at **player+146** (`mulsw`), `asr #4`,
   add player +20/24/28 → world point in d0/d1/d2.
4. Subtract world origin `$5af36/$3a/$3e`.
5. Optional look-angle update: `$24552` may write `$5adb8/$5adba` → copy to
   `$5ad8c/$5ad8e` (unless `$5aaa6` freezes).
6. **`$42290`**: build `$5aed0` from `$5ad8c/$5ad8e` via `$22b6e` + scale
   `$42d8c`; also `$5aef4` from pan; copy `$5b4e2` words → `$5ad80`.

### Cockpit `$422f8`

1. Player `$5b47c+$5abd6`.
2. `$5aef4` from player+104 (heading-related) via `$22b26`.
3. If still cockpit: 3-angle matrix `$5aee2` from player +$66 via `$22bbe`.
4. Continues into angle-select / `$5ad80` / `$5aed0` path documented earlier
   (`$2247a` region — 3-angle `$22bbe` + `$42d8c`).

### `$2d4a0` / `$2d4a6`

```
a2 = a1 + 146          ; 3x3 word matrix on scenario object
(d0,d1,d2) = M * (d3,d4,d5)   ; mulsw, asr #4
(d0,d1,d2) += a1@(20/24/28)   ; world position
```

`$2d4a6` sets `a1 = $5b47c` first; `$2d4a0` expects `a1` already set.
