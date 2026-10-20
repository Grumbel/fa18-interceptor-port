# World database and world file

Where the 3D scene gets its objects from: the static ground objects
(buildings, runways, bridges, islands, lights, ...) and the mission objects
(aircraft, ships, missiles, parachutes).  All addresses are runtime
addresses (`LOAD_BASE` = `$20000`).  Verified against the translated scene
builder (`src/native/scene.cpp`, `$30064`) and against the live object
lists of scripted runs (the decoded positions match the lists exactly).

`src/native/world.{hpp,cpp}` decode all of it, write it as a text world
file, and write an edited world file back into memory:

    ./build/fa18 --world-dump orig.world <adf>      # decode, write, exit
    ./build/fa18 --world my.world <adf>             # play with an edited world
    ./build/fa18 --world my.world --world-dump check.world <adf>  # what the game sees

`--world-dump` decodes the memory image after boot, so with `--world` it
shows the applied world. Applying an unchanged dump gives byte-identical
frames in the scenarios (checked with free1, free2, mission6_3,
mission7_combat).

## Coordinates

World positions (scenario record +20/+24/+28) are 32-bit; X and Z use the
low 29 bits:

| Bits of X / Z | Meaning |
|---------------|---------|
| 24-28 | sector (0-31), `$5ac40/$5ac42` for the player |
| 22-28 | cell (0-127), record +6/+8, `$5ac44/$5ac46` |
| 20-21 | quarter of the cell; record +10 = `(3 - qx) + 4 * (3 - qz)` ("subcell") |
| 8-21  | position inside the cell, record +12/+14 (0-16383) |

The world file uses **units of 256 world units** (the scale of record
+12/+14): a cell is 16384 units, a sector 65536, a subcell 4096.  Y
(height) grows downwards in the camera code (`$5ad5e` is negative in the
air) but the object maps have no height: static objects stand on the
ground.

## Static objects: three grid maps (hunk 66)

| Map | Row array | Grid | Subcell = | Position unit | List |
|-----|-----------|------|-----------|---------------|------|
| near | `$5778c` | 128 x 128 cells | quarter cell (4096 units) | 1 unit | `$649ba` (when `$5ab5c` = 1) |
| farA | `$5758c` | 32 x 32 sectors | cell in the sector (16384 units) | 4 units | `$63c9a` (`$5ab5c` = 0, `$5ab5d` = 0) |
| farB | `$5768c` | 32 x 32 sectors | cell in the sector | 4 units | `$6432a` (`$5ab5c` = 0, `$5ab5d` = 1) |

Each map starts with **128 row offsets** (words, relative to the map base,
all > 0; the three arrays are back to back, 256 bytes each).  Rows are Z.
A row is

    word n            ; 2 x number of cells, negative = empty row
    word x[n/2]       ; columns (X), ascending (binary search $3043c)
    long cell[n/2]    ; absolute pointers to the cell lists

and a cell list is a sequence of objects ended by `$FF`:

    byte  b0          ; low nibble: subcell, high nibble: compact position index
    byte  b1          ; bits 0-6: object type ($36094), bit 7: compact
    word  x, z        ; only if not compact: position inside the subcell

Compact objects take their position from the 16 signed byte pairs at
`$3080e` (none in the original data).  Positions are normally 0-4095.

Absolute position (units) of an object in cell (cx, cz) with subcell s:

    near:  X = cx*16384 + (3 - (s & 3))*4096 + x
    far:   X = cx*65536 + (3 - (s & 3))*16384 + 4*x      (Z alike with s >> 2)

Rules the builder (`$3034c`) imposes:

- Objects of one subcell must be **contiguous** in the list (the builder
  restarts a subcell's slot list whenever the subcell changes); the order
  of the groups is free, the order inside a group is kept.
- At most 16 objects per subcell (96-byte slot list, error 56).
- At most 70 entries per output list per frame (`$63c78`); more are dropped.
- Type 0-15 are the scenario slots (see below); static types are 16-127
  with a model in the type table.
- Cells that are present but empty still matter: they tell the builder the
  cell exists (used to insert the scenario objects standing in the cell
  into the draw order, `$30478`/`$30530`).

The **visibility bitmaps** (`$2b764`, `$2bf64`, `$2c764`, BSS, 2048 bytes
each: 128 rows of 16 bytes) are derived data: `$2f364` sets one bit per
cell listed in the maps at mission start.  The builder skips cells without
a bit.

The far maps wrap at sector 30 (`$30290`: coordinate < 0 becomes 30,
> 30 becomes 0), so sector 31 is never drawn; the near map wraps 0-127.

### Section scripts (hunk 65): which neighbour cells are drawn

The builder does not scan the map around the camera.  It runs a
precomputed **script** (a potentially-visible set in painter's order, back
to front) chosen by the camera's position class and view heading:

    byte neighbour     ; 0-20, index into $306bc (dx, dz) = the cell offset
    byte n
    byte subcells[n]   ; which subcells of that neighbour, in drawing order
    ...
    byte $FF

`$306bc` lists 21 neighbour offsets (the 3x3 block and 12 cells around
it); the cell offsets in units are `$3077a` (near) / `$306e6` (far), the
subcell offsets `$307ce` (near) / `$3073a` (far).  The camera's own cell
(neighbour 4) comes last.

| Table | Entries | Used for |
|-------|---------|----------|
| `$5654a` | 4 | near and far, view heading 60-315 degrees (`$5aa7e` = 1) |
| `$567ac` | 32 = 4 classes x 8 heading octants (`$5ab4c`) | near and far otherwise |
| `$5738e` | 64 = 4 classes x 16 | farA list at low altitude (`$5ad5e` > -40960, `$5ab4e` = 1) |

The class is `$5649c[s]` for the camera's subcell s (`$5ab49` near,
`$5ab48` far).  Scripts are written for one quadrant; the tables at
`$5646c` (subcell -> quadrant 0-3), `$5647c` (octant permutation),
`$564ac` (subcell permutation) and `$564ec` (neighbour permutation)
mirror them into the other three.

### Draw order per frame

`$211d4` terrain (`$3f284`), `$43306`, farA list (`$2fa6c`), farB list
(`$2fa7e`), then the scenario objects and the near list (`$2fc14`).  Each
list is depth sorted by `$3127c`/`$31494`.  Which lists are rebuilt is
decided by `$2f7b8` (bits of `$5ab50`, set when the camera changes cell or
octant).

## Mission objects (hunk 27)

### Scenario records `$5b47c` (16 x 512 bytes)

Record k is drawn with type k of `$36094`; when a mission creates it, one
of the 16 **templates** at `$35f54` (20 bytes, type table layout) is copied
into type slot k.  Templates (model sheet from `--mesh-dump`):

| Template | Object |
|----------|--------|
| 0, 1, 10 | missiles |
| 2, 11, 13 | parachute / rescue pod |
| 3, 4, 6, 7 | fighters |
| 5, 14, 15 | airliner (Air Force One) |
| 8, 9 | carrier Enterprise |
| 12 | ship |

Known record fields (the rest are flight / AI state):

| Off | Size | Use |
|-----|------|-----|
| +0 | word | flags; bit 15 = not drawn, bit 6 (byte +1) = in use |
| +1 | byte | bits 0, 3 from the spawn flags; bit 4 = cell lists dirty; bit 2 = draw from the scenario list `$63c7a` (cleared when the builder placed the record in the sorted list of its cell) |
| +5 | byte | 8 = spawn flag bit 14 |
| +6/+8 | word | cell x / z |
| +10/+11 | byte | subcell (quarter cell) / cell in the sector |
| +12/+14 | word | position in the cell (units) |
| +16 | long | height (units) |
| +20/+24/+28 | long | world position X / Y / Z |
| +44..+55 | | current waypoint (cell x, z, offset x, z, long height) |
| +56 | byte | target: `$FF` = waypoint from the route, else `$80` + slot |
| +58 | byte | route (position table index) |
| +76, +124 | | state (damage, LOD choice of the `$369cc` draw routines) |
| +93 | byte | spawn order |
| +94 | byte | own slot |
| +98 | byte | kind (spawn word 1); high nibble = class (`$10` aircraft counted as enemies, `$20` carrier, `$30` ...) |
| +102..+106 | word | attitude angles |
| +146 | 9 words | orientation matrix (rotates the articulated mesh) |

### Kinds (record +98)

There are no tables indexed by the kind: about 115 places in the AI,
weapons, view and draw code compare it, either the class (`andi #$f0`:
`$10`, `$20`, `$30`) or exact values (0, 1, `$10`, `$11`, `$14`, `$15`,
`$20`, `$30`).  Kinds in the original spawn tables:

| Kind | Template | Where |
|------|----------|-------|
| `$10` | 3 (fighter) | mission 2 (one aircraft), mission 5 (two) |
| `$12`, `$13` | 6, 7 (fighters) | hostile fighters, missions 3-7 |
| `$14` | 5 (airliner) | mission 4 |
| `$15` | 10 | mission 7 |
| `$16` | 14 (airliner) | mission 3 variant 1 (Air Force One) |
| `$20` | 8 (carrier) | every mission, slot 14 |
| `$32` | 13 (parachute) | mission 6 (downed pilot) |

So a new aircraft can reuse an existing kind with any template (and its
behaviour); a new kind needs code that checks for it.

### Spawn entries (10 bytes, `$3d320`)

| Word | Meaning |
|------|---------|
| 0 | template: byte offset into `$35f54` (template x 20) |
| 1 | kind -> record +98 |
| 2 | bits 0-6 slot; bits 8-14 -> +58; bit 15: the first waypoint is position (bits 8-14), else +56 = `$80` + bits 8-14 |
| 3 | start position: byte offset into the position table (index x 2) |
| 4 | flags: bits 0-6 minimum enemy setting (`$5ab9f`) for the entry to spawn; bit 7 halve the jitter; bits 8-11 only in this mission (`$5ab9e`), 0 = all; bit 12 random start offset (`$5adf0`); bit 13 record +1 bit 0; bit 14 record +5 = 8; bit 15 record +1 bit 3 (else class `$10` counts as an enemy, `$5aba1`) |

### Mission table `$3dfba`

10 missions x 3 variants x (word list offset from `$3dfba`, word max count
-> `$5aba2`).  The variant comes from `$5adf0` (`$3cf0e`); a list starts
with 4 word offsets, one per level (`$2d03c` config byte 18 + mission, the
pilot's progress).  The first spawn of a level list is always created, the
following ones while their flags allow (word 4 bits 0-6).  Missions 126
and 127 use entry 10; 125 copies the player record instead (`$3d0b2`).

### Position table `$3ddc8`

60 word offsets (from `$3ddc8`) to 10-byte **waypoints**: cell x, cell z,
offset x, offset z (units), height (units).  A spawn's start position is
one waypoint; a route (record +58) continues with the following waypoints
until one starts with a negative word (`$37bda`, `$40c20`).  The data is
packed: routes run into the zone and spawn data that follow them.

### Trigger zones `$3df08`

Up to 8 pointers (the original has 4), ended by a negative word.  A zone is
`x1, x2, z1, z2` (player cell bounds, inclusive), a count and that many
spawn entries; `$3d182` creates them when the player enters the zone (once,
bit in `$5aa95`) and resets their routes when the player leaves.

### Adding objects

- **Buildings / ground objects**: add `obj` lines to a map.  The rows are
  rebuilt in hunk 66 (1080 bytes; identical rows are shared) and the cell
  lists go to a block at the top of RAM, so the number of objects is only
  limited per subcell and per frame list.
- **Mission objects**: hunk 27 has no free space and its offsets are
  16-bit relative to code-embedded bases, so spawn lists, zones and routes
  are patched in place (same counts); existing entries can be retargeted
  (template, kind, position, route).  *New* objects are `newspawn` lines:
  the native mission setup (`$3cf0e`, `src/native/missions.cpp`) creates
  them after the mission's own list with the original `$3d2ea`; the start
  position is put into the record of position 1 for the duration of the
  call, so cell, subcell, height and the initial aiming (`$3cfec`) are
  computed by the original.  Choose a free scenario slot (16 records; the
  carrier uses 14, the missions 4-12) and an existing kind (see above).

## World file

Text, `#` starts a comment.  First line `fa18-world 1`.  Sections that a
file leaves out keep the original data, so an override file can contain
only `map near`.

    map near|farA|farB          ; replaces the whole map
    cell X Z                    ; grid column / row (cells or sectors)
      obj TYPE X Z [sub=S] [hi=N]   ; absolute position in units
      objc TYPE SUBCELL INDEX       ; compact entry
    scripts 5654a|567ac|5738e   ; replaces a whole script table
    script I N:SUBCELLS ...     ; e.g. "4:fbe7ad369c258140", "-" = none
    mission M V list=ADDR max=N levels=A,B,C,D
    spawns ADDR                 ; then "spawn" lines, same count
      spawn TMPL KIND SLOT POS FLAGS   ; five hex words
    pos INDEX OFFSET            ; position table
    wp OFFSET CX CZ OX OZ H     ; waypoint record
    zone ADDR X1 X2 Z1 Z2       ; then its "spawn" lines
    newspawn M TMPL KIND SLOT FLAGS CX CZ OX OZ H   ; extra object (M = $5ab9e, 0 = all)
    template I DRAW MODEL LOD LOD2 LOD3   ; information only

`sub=` is only written when an object's position does not identify its
subcell (positions outside 0-4095); with `sub=` the position is relative
to that subcell's corner.  Mission data is written only where it differs
from the original, then read back: edits of overlapping data that
contradict each other are rejected.
