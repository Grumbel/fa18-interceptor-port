# Ground, sea, sky and the view transform

How the original draws everything in the 3D view that is not an object,
and what a renderer needs to reproduce it in world space.  Runtime
addresses (`LOAD_BASE` = `$20000`).  Supersedes the guesses in
`horizon.md` (which remains as a research log); the decoder is
`src/native/terrain.{hpp,cpp}`.  Native translations: `$3f284` in
`ground.cpp`, `$42f62`/`$42f6a` in `sky.cpp`, `$4217c` and the matrix
builders in `view.cpp`.

## Summary

The game has no horizon line and no ground mesh.  Per frame (`$44994`,
`$3f284`, `$42f6a`):

1. **Backdrop**: the whole 3D view is filled with palette index **4**
   (plane 2 set, the others clear): *land*.  In open-ocean views
   (`$5ab93` = 1) plane 1 is set too: index **6**, *sea*.
2. **Water**: the terrain tiles of `$3f284` paint flat polygons in the
   plane Y = 0 with plane 1 set: 4 -> **6** (sea).  So the terrain tables
   describe the *water* of the bay and the ocean on a land backdrop.
3. **Sky**: a huge quad at the camera's height *above* it (the ground
   plane mirrored at the eye) is clipped and filled with plane 0 set: 4 ->
   **5** (sky; `$4506e` clears plane 1 again in open-ocean views).  Its
   lower edge is the horizon.
4. Objects (`docs/world_format.md`) are drawn on top.

With the free1 palette: 4 = `#151` (land), 5 = `#447` (sky), 6 = `#036`
(sea).  So the equivalent world-space scene is: a land plane at Y = 0,
the water polygons on it, and the sky colour above the horizon.  The
horizon then falls out of the perspective; it is not reconstructed.

## View transform (`$4217c`)

Called every in-flight frame before the camera position (`$2f4a6`).

| Variable | Meaning |
|----------|---------|
| `$5ad5a/$5ad5e/$5ad62` | camera world position (X, Y, Z); Y negative above the sea |
| `$5ad70` | camera height used by the 3D code (`-$5ad70` = altitude) |
| `$5aed0` | 3x3 word matrix, world -> view, **rows scaled** by `$5ad36/$5ad38/$5ad3a` (168, 252, 128 in flight) |
| `$5aee2` | 3x3 player attitude matrix (cockpit, from record +102..106) |
| `$5af18` | 3x3 matrix of the view direction offset (cockpit look modes) |
| `$5aef4` | pan rotation (record +104 or `$5ad8e`), `$42b26` |

- **External view** (`$5aa7d` != 0, `$4219a`): eye = subject record
  position + its matrix (+146) times a seat offset (0,0,1) / (0,0,5) /
  (0,0,-4), minus the world origin `$5af36`.  `$24552` turns the
  eye-to-subject vector into two angles `$5ad8c` (pitch) / `$5ad8e`
  (heading), 28800 = 360 degrees; `$42b6e` builds `$5aed0` from them
  (`horizon.md`, "`$22b6e` matrix"), `$42d8c` scales the rows.
- **Cockpit** (`$422f8`): `$5aee2` from the record's three angles
  (`$42bbe`); look modes (`$5aa9f`, `$5aaa9`) select angle offsets from
  the tables `$424a2` (aircraft) / `$424e4` (class `$30`) into `$5af18`;
  `$5aed0` is the product, row-scaled.

Projection (`$4389c`, `project_screen_body`): view (vx, vy, vz) ->
`sx = 159 - 160 vx / vz`, `sy = 180 - (90 + 90 vy / vz)`; rows below
`$5ac7c` belong to the cockpit.  Products with the matrix are `muls` then
`asr #8` (matrix entries are 1/256 fixed point times the row scale).
A renderer can therefore use `R = $5aed0` with the row scales divided out
as the camera rotation and the row scales as the projection's x/y scale.

Objects reach the renderer camera-relative: `$5ae28..$5ae30` (long) at
the object dispatch in `$2fc14`, already shifted right by the LOD shift
`$5adb0`.

## Sky quad (`$42f6a` / `$42f62`)

`$42f70` transforms the four corners of `$42f38`

    (10240, -9216)  (-11008, -9728)  (-10240, 9216)  (9728, 11008)    ; (X, Z)

with Y = `swap($5ad5e) >> 2`, i.e. the camera's own Y in units of 2^18
world units, by the matrix (`$5aed0`, or `$5aee2` for the second pass in
the cockpit).  Without the translation this is a plane at the camera's
height *above* the camera (the ground plane is at -Y), 10000 x 2^18 world
units wide: its clipped image is exactly the part of the view above the
horizon.  `$43364` clips it against the view; the case tree at `$42ff8`
rebuilds `$60684` as a screen polygon (return 0) or gives up (return 1,
e.g. when it is entirely off screen).  `$44dfe` fills it (plane 0 set);
in open-ocean views `$4506e` (`d0` = 8, `d3` = 0) clears plane 1 inside
it.  In the cockpit the second pass (`$42f62`, attitude matrix) is copied
to `$60726` for the HUD.

## Terrain tiles (`$3f284`)

Two passes, the far one only when the scaled altitude is above 1016:

| Pass | Grid | Tile size | Vertex unit | Table |
|------|------|-----------|-------------|-------|
| far  | 8 x 8, tile 4 starts at X = 0 | 2^28 world units | 65536 | `$57fa4` (row stride 16) |
| near | 32 x 32 (the sectors) | 2^24 | 4096 | `$58168` (row stride 64) |

Grid entries are word offsets from the table to a **tile record**:

    long  coarse          ; < 0: no water; else pointer to a simplified list
    list                  ; the full list
    list:  n, n x (x, z)          n = 1..18: polygon (tile-relative)
           $8000|t                 the following polygons need altitude >= 4t
           -1                      end

All polygons are in the plane Y = 0; tile corners are drawn 4224 wide
(overlapping the 4096 tile) to avoid gaps.  Scaled altitude (frame
`-40`): `alt = -$5ad70`, and unless `$5aad5` is set or alt > 524272:
`alt = (alt >> 4) * (32768 / max($5ad3a, 2)) >> 4` (`terrain::scaled_altitude`).

Which tiles are drawn, in which order, comes from **pattern lists**
(bytes, `$FF` end, bit 7 = last entry) of tile indices into the delta
table `$3e6e8` (signed byte pairs dx, dz around the camera's tile), chosen
by:

- far pass: altitude <= 20480: `$3e81a` + 4 x class, else `$3e71a` + 16 x
  class (class = `(3 - bx) + 4 (3 - bz)` for the camera's 2^26 block
  (bx, bz) inside the far tile, like `$5ab48`);
- near pass: altitude > 36864: `$3e85a` (<= 65536) or `$3e8aa`; else
  `$3efc4` / `$3ecc4` / `$3e8c4` (altitude <= 12288 / <= 18432 / above)
  indexed by the camera's cell in the sector (`$5ab48`) and the heading
  octant (`$5ab4c`).  Below altitude 928 the lists `$3f244` / `$3f204` /
  `$3f1c4` (altitude <= 16 / <= 352 / above) give four octants per
  `$5ab48` for which only the own tile is drawn (`$3f490`, `$5ab95` = 1).

Near pass details:
- below altitude 3200 the transform is shifted up by 1 (below 1024 by 2
  for the own tile) for precision (frame `-32`);
- the coarse list is used for tiles farther than `$3f5e0[min(alt >> 8,
  17)] << 16` (scaled like the altitude) from the camera, and for the own
  tile when `$5ab94` is set.

Colour: `$5aaa8` = 0 (default): mask 2, set plane 1 (water = backdrop | 2);
`$5aaa8` != 0 (keyboard toggle `$2ef4e`): colour `$5ac4c` = 6 in all
planes.  The frame path skips the terrain when `$5ab93` is set and
`$5aaa8` is clear (`$211c2`): open-ocean view, the backdrop is already
sea.

## Zone tables (`$33120` -> `$5ab93`, `$33140` -> `$5ab94`)

`$33160` matches the camera against a table (`$331f0` resp. `$33420`):

    word zkeys[], -1          ; sector Z ($5ac42)
    word zoffs[]              ; per key, from the table start
      -> word xkeys[], -1     ; sector X ($5ac40)
         word xoffs[]
           -> records until a negative word:
              word  kind      ; 0: altitude <= limit, else altitude >= limit
              long  limit     ; compared with -$5ad70
              bytes: { cell ($5ab48), octants ($5ab4c)..., <0 }..., <0  (word aligned)

The result is 1 when one record's altitude test passes and its list
contains the camera's cell-in-sector with the current heading octant.
`$5ab93` = open-ocean view (sea backdrop, no terrain);
`$5ab94` = use the coarse mesh for the own tile.

## For a world-space renderer

- Clear to the sky colour (palette 5 of the live palette).
- Draw a land plane at Y = 0 (palette 4) out to the far grid's extent.
- Draw the water polygons (palette 6; with `$5aaa8` set: palette
  `$5ac4c`) on it: far grid, then near grid, full lists (the coarse lists
  and the pattern lists are only the original's speed trade-offs), with
  the altitude thresholds.  They are coplanar with the land plane: draw
  them after it with the depth test off for the ground layer (or a
  polygon offset).
- Objects that lie on the ground (runways, islands, flat patches) are
  coplanar with the ground too and need the same treatment.
- The open-ocean flag only changes what the original *skips*; drawing
  the full water polygons gives the same picture.
