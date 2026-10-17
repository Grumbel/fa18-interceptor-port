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
