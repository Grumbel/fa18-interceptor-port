# Horizon / sea–sky drawing

Research notes from the relocated executable (LOAD_BASE `$20000`).
Addresses below are runtime addresses.

## Call graph

```
game frame path
  $211b4  jsr $42f48          ; horizon dispatcher
            │
            ├─ bit13 of $5abca clear  →  jsr $44994   (simple blitter plane-clear)
            └─ bit13 set              →  bsr $23250   (complex path)

fill path (from $44994 body / $44b00)
  $44af4  jsr $42f6a          ; build sea quad with matrix $5aed0
  $44b22  jsr $42f62          ; same with matrix $5aee2 (when $5aa7d==0)
```

`$42f6a` / `$42f62` share a body at `$42f70`.  They return status in
condition codes; callers branch on the result (`bne` at `$44afa`).

## Two matrices

| Address  | Role |
|----------|------|
| `$5aed0` | Primary 3×3 view matrix used by `rot_out` / object projection |
| `$5aee2` | Second 3×3 immediately after (`$5aed0 + 9 words`) |

`$42f6a` uses `$5aed0`; `$42f62` uses `$5aee2`.

`rot_out` semantics (confirmed by native translation):

```
view = M * world     (M stored row-major, asr #8 after accumulate)
```

Level-flight dump of `$5aed0`:

```
[168   0    0]
[  0   0  252]
[  0 −128   0]
```

So world +X → view +X, world +Y → view −Z, world +Z → view +Y.
World **+Z is the up axis** for this matrix layout.

## Sea-quad builder (`$42f6a` / `$42f62`)

1. Corner table at `$42f38` — four fixed world (X, Z) pairs (Y comes from camera):

   | # | X      | Z      |
   |---|--------|--------|
   | 0 |  10240 |  −9216 |
   | 1 | −11008 |  −9728 |
   | 2 | −10240 |   9216 |
   | 3 |   9728 |  11008 |

2. For each corner:
   - `d2 = table.x`
   - `d3 = swap($5ad5e) >> 2`   ; camera Y (altitude), high word, /4
   - `d4 = table.z`
   - Transform `(d2,d3,d4)` by the chosen matrix → view `(vx,vy,vz)`
   - Store three words at `(a3)+`, then `a3 += 26` → **stride 32 bytes**

3. Clear 16 bytes at `$63b44`, then `jsr $43364`.

`$43364` reads the four view-space verts from `$60684` (with a different
internal stride of 16 for its own temp at `$60c84`), clips edges against
the view frustum, and fills a result structure at `$63b44`:

```
$63b44:
  +0,+2,+4,+6   word flags (which edges/corners survived)
  +8,+10,+12,+14  word screen-ish coordinates used by the case dispatch
```

## Screen-space polygon rebuild

After `$43364`, a large case tree (from `$22ff8`) inspects the flags and
rebuilds `$60684` as a **count-prefixed screen-space polygon**:

```
$60684:  word n          ; 3, 4, or 5
         n × (word x, word y)   ; already in screen pixels
```

Helpers that emit corners of the 320×180 view:

| Addr    | Writes          |
|---------|-----------------|
| `$232e8`| `(0, 0)`        |
| `$232ec`| `(319, 0)`      |
| `$232f4`| `(319, 179)`    |
| `$232fe`| `(0, 179)`      |
| `$232b8`| two points from `$60c84`, with `x' = 319−x`, `y' = 179−y` |

The 319/179 subtraction matches the post-flip in `project_screen_body`
(`$438b6`):

```
sx = 319 - (160 + 160*(d0/d2))
sy = 180 - (90  +  90*(d1/d2))   ; +1 then clamp omitted in the helper
```

So by the time a fill runs, **horizon geometry is already in screen space**.
There is no infinite-plane test left to re-derive from the matrix.

## Blitter fill (`$44994` / `$44f46`)

When bit13 of `$5abca` is clear, `$44994` does a direct blitter plane-clear
of display plane 1 over the view rectangle (height from `$5ac7c`), using
plane pointers at `$5a9ae`.  It does **not** consume the `$60684` polygon.

When the complex path has run, `$44b00` / `$44f46` can fill using the
screen-space polygon.  `$5ab93` (set by `$33120` sky/ground table match)
selects whether a second colour (ground/sea) is active.

## Why matrix-only reconstruction fails

1. The sea is a **finite quad** at camera altitude, not an infinite plane
   through the origin.  Even a correct `N = M*(0,0,1)` vanishing line is
   only an approximation of the true clipped polygon.
2. After `$43364` the game further **clips to the view rectangle** and may
   emit 3–5 screen verts including view corners — that shape cannot be
   recovered from N alone.
3. `$60684` layout **changes**: first the 32-byte-stride view-space form
   from `$42f6a`, then the count-prefixed screen-space form.  Capture that
   reads a count at `$60684` only works after the rebuild, not right after
   the matrix multiply.
4. The simple `$44994` path never builds a polygon at all.

## Correct observation points for a remaster

| When | What to read |
|------|----------------|
| After `$42f6a` returns success | `$60684` screen-space poly (count + verts) — this is what the fill uses |
| During `$44994` simple path | No poly; the fill is a full-view plane-1 clear bounded by `$5ac7c` |
| `$5ab93` | 0 = sky only; ≠0 = ground/sea second colour |
| `$5ac7c` | Last row of the 3D view (0-based); rows = value+1 |

## Related addresses

| Addr | Meaning |
|------|---------|
| `$5ad5e` | Camera Y used as sea-quad height |
| `$5abca` | Flags; bit13 selects simple vs complex horizon |
| `$5aa7d` | Selects `$42f62` (`$5aee2`) vs skip |
| `$60c84` | Temp screen points produced inside `$43364` |
| `$63b44` | Edge-survival flags + coords after clip |
| `$33120` | Sets `$5ab93` via table match on `$5ac40/$5ac42` |

## Runtime observation (free1 scenario)

Under `tests/scenarios/free1.txt` (frames ~2600–6000):

- `$42f48` always takes the **simple** branch (`$5abca` bit13 = 0).
- `$44994` runs every frame; `$5ab93` stays 0 (sky-only table match).
- `$42f6a` / `$232aa` are **never** reached — no count-prefixed sea poly
  is built.
- Scene capture therefore sees `fill=0`; visible structure is mesh
  `poly`/`line` only.

Implication: in free flight the painted sea/sky split is **not** the
complex `$60684` polygon.  It comes from the `$44994` plane blits and/or
3D ground meshes.  Further RE of `$44994`’s per-plane minterms and any
prior edge setup is required before a Fill2d can represent that path.

## Capture points (implemented)

| Hook | Runtime addr | When |
|------|--------------|------|
| `$44994` entry | `$44994` | Simple-path plane clears start |
| `$44dfe` entry | `$44dfe` | **Primary capture** — `$60684` holds count-prefixed screen-space sea poly |
| `$432aa` / `$432b6` | success/fail RTS of `$42f6a` | Unreliable under recomp (translated blocks skip exact RTS PC) |

Important: `$44994` **itself** calls `$42f6a` after the plane clears.  The
“simple path” is not poly-free — it is plane-clears *plus* the complex
sea-quad builder.  `$5ab93` only gates the final `$2506e` plane-1 fill of
that poly; the poly geometry is still built and `$44dfe` still runs.

### free1 runtime

`$44dfe` fires with `n` in 3..13 every frame.  Software scene now reports
`fill≥1` (sky injection + sea poly).

## free1 observation (2026-10-17, deeper)

### Call path under recomp

| Event | Count (≈3100 frames) |
|-------|----------------------|
| `$44994` entry (simple path) | every frame |
| `$42f6a` entry | most frames |
| `$44dfe` with return `$44b00` (horizon) | **never** |
| `$44dfe` with return `$44b5e` (general `$44b50` fill) | many (mesh 2D fills) |

So `$42f6a` runs but **always fails the clip** before `$44dfe` on the
horizon path.  No count-prefixed sea poly is ever installed for free1.

Camera Y at `$5ad5e` is large negative fixed-point (e.g. `-50331648`).
Sea-corner Y is `swap($5ad5e)>>2` — same height as the camera in the
builder’s coordinate system — so the “sea quad” is coplanar with the
eye and routinely clips away.

### What the low-res frame actually shows

Center column (x=160) at free1 frame ~3003:

```
y=0..45   #003366  sky blue
y=50..140 #115511  ground green
y=145+    #003366  (below view / other)
```

There **is** a sky/ground split around y≈45–50.  It is **not** produced
by the `$42f6a` sea poly (that path never reaches `$44dfe`).  Candidates:

1. **3D mesh fills** drawn through `$44b50` / `$38c0c` (scene capture often
   reports `poly=0` early — may be a capture gap, or ground uses another
   path).
2. **Blitter line+fill** with edge tables set outside `$42f6a`.
3. Something in the plane-clear sequence at `$44994` that is not a full
   rectangle (modulo/offset quirks) — less likely given BLTSIZE math.

### Capture mistake corrected

Hooking all `$44dfe` calls captured **mesh 2D fills** (return `$44b5e`),
not the sea poly.  Those verts are small scattered polygons (buildings /
terrain details), colour `#003300`.  Filtering to return `$44b00` yields
zero captures in free1 — consistent with `$42f6a` always failing.

### Next RE steps

1. Why `$42f6a` fails: dump `$63b44` flags and view-space corners at
   `$60684` on the failure RTS (`$432b6`).
2. Trace what paints `#115511` into the playfield — watch blitter writes
   or `$38c0c` / `$44b50` with ground colour after `$44994`.
3. Confirm whether ground is a large projected mesh (capture gap) or a
   dedicated blitter horizon edge fill.

## Deeper free1 findings (plane indices + $2506e)

### Palette identity of the split

| Region | Playfield index | COLOR | RGB |
|--------|-----------------|-------|-----|
| Sky (y≈0–45) | **6** | `$036` | `#003366` |
| Ground (y≈50–140) | **4** | `$151` | `#115511` |

Index 6 = planes 1+2; index 4 = plane 2 only.  So relative to a full-view
plane-2 backdrop, **plane 1 is set in the sky and clear in the ground**.

### `$2506e` is busy — but not from the horizon gate

`$2506e` runs often with `d0=8` (plane-1 op) and **`d3≠0`** (e.g. 1, 513,
2054…).  Those calls come from the **`$44b50` general fill path**
(`$44bae` / `$44b98` / …), which loads `d3` from `$5a9e0`.

The horizon-only call site (`$44b0a`: `moveq #8,d0; moveq #0,d3`) is
still gated on `$5ab93≠0` and was **not** observed in free1.

### `$3f284` terrain

Always entered after `$42f48` when `$5ab93==0`.  This is a multi-LOD
terrain/tile drawer (tables at `$1e6e8` etc., driven by altitude at
`$5ad70`).  It is a primary candidate for the green ground band together
with mesh fills through `$44b50`.

### Implications for a remaster

In free flight the visible sky/ground split is **not** the `$42f6a` sea
polygon.  It is the residual of:

1. `$44994` full-view plane blits, then
2. `$3f284` terrain, then
3. `$2fc14` object meshes / `$44b50` fills that clear or set plane 1

Reproducing that in the software renderer means either:

- capturing terrain + mesh fills that actually paint indices 4 and 6, or
- using the already-painted low-res view as the backdrop (observed truth)
  and only replacing mesh edges at hi-res.

The sea-quad path remains important for modes where `$5ab93≠0` and
`$42f6a` succeeds (e.g. other missions / altitudes).

## World model — ground plane and horizon (correct axes)

### Coordinates

| Quantity | Address / form | Meaning |
|----------|----------------|---------|
| Camera X/Y/Z | `$5ad5a` / `$5ad5e` / `$5ad62` | World position |
| Altitude | `-$5ad5e` | Height above sea (`view_change_flags`) |
| Ground / sea | world **Y = 0** | Constant-altitude plane |
| View matrix R | `$5aed0` 3×3 row-major | `V = R * (P − C)`, products `asr #8` |

Sea-quad builder `$42f6a` places corners at fixed world X/Z with
`Y = swap($5ad5e)>>2` (high word of camera Y, scaled) — a finite patch
of the same constant-Y plane, not a different orientation.

### Level-flight R (from dumps)

```
[168   0    0]     world +X → view +X
[  0   0  252]     world +Z → view +Y
[  0 −128   0]     world +Y → view −Z
```

Column 1 = `(m[1], m[4], m[7]) = (0, 0, −128)` is the image of world **+Y**.

### Horizon line

Screen ray (inverse of `project_screen_body`):

```
vx/vz = (159 − sx) / 160
vy/vz = ( 90 − sy) /  90
```

Direction in world (camera-relative): `D = Rᵀ V` (R rotation).
Component along altitude: `D_y = column1 · V`.

Horizon: `column1 · (vx, vy, vz) = 0`.
Ground (ray hits Y=0 in front): `column1 · (vx/vz, vy/vz, 1) > 0`
when `C.y < 0` (normal flight).

Level flight → `D_y ∝ −vy` → horizon at `sy = 90`; below = ground.

**Earlier error:** using column 2 (world +Z → view up) treated the wrong
plane.  Column 2 is the *visual up* axis in view space after attitude is
applied; the *ground* is still world Y = 0.

## Player vs camera (free1 live dumps, frames 3003–3066)

### View mode

`$5aa7d = 1` → **external / chase view**, not cockpit.
External pan/tilt live at `$5ad8c` / `$5ad8e` (units 0..28799 = 360°).

### Positions are different

| | X | Y | Z |
|-|---|---|---|
| Camera `$5ad5a/5e/62` | ~0…−4e6 | ~−5e7…−1e7 | varies |
| Player `base+$14/$18/$1c` | 273963296 | **1800** | 279184624 |

Player sits near the ground (`Y=1800`). Camera is far above (`altitude = -$5ad5e` ≈ 1e7–5e7) and moves while the player stays fixed.

Player object base = `$5b47c + $5abd6` (here `$5abd6=0` → `$5b47c`).

### Orientation sources

| Slot | Role in free1 |
|------|----------------|
| `$5ad8c` / `$5ad8e` | External tilt / pan; **drive `$5aed0`** via `$22b6e` then row-scale `$42d8c` |
| Scales `$5ad36/38/3a` | 168, 252, 128 (FOV-related row scales on `$5aed0`) |
| Player `+$66/$68/$6A` | 0, 2240, 0 — **frozen** (parked) |
| Player `+$80` / `+$92` | Body matrices — **frozen** (yawed relative to world) |
| `$5aee2` | All zero in these dumps |
| `$5aed0` | **Live camera view matrix** (tracks external angles) |

Builder path for `$5aed0` (from `$4229c`):

```
d0 = $5ad8c; d2 = $5ad8e
lea $5aed0, a1
bsr $22b6e          ; 2-angle matrix (tilt/pan)
lea $5aed0, a1
bsr $42d8c          ; scale rows by $5ad36/$5ad38/$5ad3a
```

### Predicted vs observed horizon row

Infinite-plane predictor at screen centre (`column1 · ray = 0`):

```
sy ≈ 90 + 90 * m[7] / m[4]     (when m[1]≈0)
```

| frame | `$5ad8c/$8e` | `$5aed0` col1-ish | pred sy | observed hrow (idx leaves 6) |
|------:|-------------:|-------------------:|--------:|-----------------------------:|
| 3003 | 7200 / 0 | (0,0,−128) | 90 | **46** |
| 3030 | 7030 / 560 | (0,8,−128) | −1350 | **0** |
| 3046 | 6499 / 2542 | (−10,38,−127) | −211 | **57** |
| 3062 | 6052 / 3030 | (−20,62,−124) | −90 | **54** |

Even when `$5aed0` is level, observed sky/ground split is at **y≈46**, not 90. When the matrix pitches, predicted infinite-plane sy and observed hrow **move in related ways but disagree in magnitude**.

### Implications

1. free1’s painted split is **not** the pure infinite world-Y=0 horizon of `$5aed0` alone — terrain / other fills shift it (especially near the player’s low altitude while the camera is high).
2. `$5aed0` **is** the live external camera orientation; player `+$92` is not.
3. Pitch “range feels wrong” if the software horizon tracks `$5aed0`’s infinite plane while the user is looking at **terrain-dominated** low-res ground.
4. Cockpit (`$5aa7d=0`) is a different path (player angles → `$5aee2` builder at `$22312`); not exercised by these free1 dumps.

### Angle units (reference)

| System | Full circle | Where |
|--------|-------------|--------|
| External view | 28800 | `$5ad8c/$5ad8e`, `look_angles` |
| Matrix builder after `lsr #3` | 3600 (=28800/8) | `$22b6e` inputs |
| Player table angles | 7200 | matrix builders A/B in transform docs |

## Camera position (`$2f4a6`) — cockpit vs external

### External (`$5aa7d ≠ 0`)

```
call $2f220
$5ad74/$78/$7c ← $5af36/$3a/$3e   ; eye from external tracker
read back $5ad5a/$5e/$62           ; already maintained elsewhere
```

Camera is independent of the player record (matches free1 dumps).

### Cockpit (`$5aa7d == 0`)

Player record `a2 = $5b47c + $5abd6`:

1. Local eye offset in aircraft frame from mode byte `a2+98`  
   (e.g. `(0,4,18)` or `(0,5,20)` or `(0,1,−5)`).
2. Rotate offset by the 3×3 words at `a2+146..162` (`mulsw`, `asr #6`).
3. World eye = player position (`a2+20/24/28`) + rotated offset  
   → stored at `$5ad74/$78/$7c`.
4. `$5ad5a/$5e/$62` = **negated** world eye  
   (X/Z of player contrib masked with `$3FFFFF` before add).

So in cockpit, `$5ad5*` is `-eye_world`, not a free camera.

## Two builders for `$5aed0`

| Path | Entry | Angles | Builder | When |
|------|-------|--------|---------|------|
| External-style | `$4229c` | `$5ad8c`, `$5ad8e` (2) | `$22b6e` then `$42d8c` | free1 external |
| Attitude-style | `$2247a` | `$5ad82`, `$5ad86`, `$5ad8a` (3) | `$22bbe` then `$42d8c` | after player angles → `$5ad80` |

`$5ad80` receives player `+$66/$68/$6A` (or derived view angles) on the
cockpit / alternate path (`$2237e`, `$22472`).

`$5aee2`: 3-angle matrix from player `+$66` when `$5aa7d==0` (`$22326`).  
`$5af18`: another 3-angle matrix (HUD / secondary, `$22450`).

Both `$5aed0` builders finish with **row scale** `$42d8c` using  
`$5ad36/$5ad38/$5ad3a` (free1: 168, 252, 128).

## Player record (relevant fields)

| Offset | Role |
|--------|------|
| +$14/$18/$1c | World position X/Y/Z |
| +$66/$68/$6A | Attitude angles (table units) |
| +$80 | Body matrix A |
| +$92 | Body matrix B |
| +$92 region +146 | Eye-offset rotation basis (cockpit) |
| +98 | View/seat mode byte (selects eye offset) |

`$5abd6` = index × stride into `$5b47c` (0 for the free1 player).

## Angle units and `$22b6e` (2-angle view matrix)

### Units

| Stage | Full circle | Notes |
|-------|-------------|--------|
| External `$5ad8c/$5ad8e` | **28800** | 360°; `look_angles` / pan |
| After `lsr #3` into builders | **3600** | 28800/8; builders then `addw dN,dN` for table index |
| Sin table `$538e8` | index 0..1800 = 0..90° | Q14: **16384 = 1.0** |

`$22dd6`: `addw d0,d0` then quadrant logic; comparisons use the doubled
value (1800/3600/5400/7200).  Effective: post-`lsr` **3600 = 360°**.

free1 level: `$5ad8c=7200` → 900 after `lsr` → **90°** pitch, pan 0.

### `$22b6e` matrix (pitch=`d0`, pan=`d2` after lsr)

After `$22dd6`: `(s0,c0)=(sin,cos) pitch`, `(s2,c2)=(sin,cos) pan`.

```
m00 = c2>>6      m01 = 0        m02 = s2>>6
m10 = −(s2·s0)   m11 = c0>>6    m12 = (c2·s0)     ; · = mulsw → swap → asr.w #4
m20 = −(s2·c0)   m21 = −s0>>6   m22 = (c2·c0)
```

Then `$42d8c` scales rows by `$5ad36/$5ad38/$5ad3a` (`mulsw` + `asr #8`).

### free1 level check

pitch=90° `(s0,c0)=(16384,0)`, pan=0 `(s2,c2)=(0,16384)`, scales `(168,252,128)`:

```
pre-scale  [256 0 0 / 0 0 256 / 0 −256 0]
scaled     [168 0 0 / 0 0 252 / 0 −128 0]   ← matches live $5aed0
```

So external “level” is **pitch angle 90°** in this convention (not 0°).

### `$22bbe` (3-angle)

`lsr #3` on d0,d2,d4; `$22dd6` + `$22eba` → three sin/cos pairs; full
Euler product into `(a1)+`.  Used for `$5aed0` when angles come from
`$5ad82/86/8a`, and for `$5aee2` / `$5af18`.
