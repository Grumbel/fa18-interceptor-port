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
