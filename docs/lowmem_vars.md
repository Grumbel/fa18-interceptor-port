# Low-Memory Graphics / State Variables

Tracked absolute addresses used heavily by the blitter and draw path.

## $36e – Draw / graphics mode

**Type:** word  
**Writers:** dozens of sites across primary and overlay hunks  

Sample values written:

| Value | Example site | Context |
|-------|--------------|---------|
| `$001b` | 0x28274 (hunk 35 entry) | blitter mode stub |
| `$0001`–`$000a` | 0x1c302–0x1c8e6 | sequential mode IDs |
| `$003f` | 0x1b0c | |
| `$0028` | 0xe010 | |
| `$0046` | 0x2c6e2 | |

Acts as a **current graphics operation / draw-mode ID**.  Different
callers set it before entering shared blitter primitives so the engine
knows which style of draw (line, fill, clear, HUD, …) to perform.

## $324 – Plane / pen selector

**Type:** word  
**Range:** typically `0x00`–`0x0f`  

Common values: 0, 1, 2, 3, 4, 6, 7, 8, 9, 0xa, 0xc, 0xd, 0xf.

Likely a **bitplane mask or colour/pen index** selected for the next
blitter operation.  Paired with `$b6` plane enables.

## $b6 – Bitplane enable mask

**Type:** long (sometimes written as word)  

| Value | Meaning (inferred) |
|-------|--------------------|
| `$000fffff` | enable low 20 bits – standard full mask |
| `$ffffffff` | all bits |
| `$0007ffff` | slightly narrower mask |
| `$0000000f` / `$00000001` | single/few planes |

Controls **which bitplanes the blitter will touch**.  `$fffff` matches
a 5-plane playfield (bits 0–19 cover five 4-bit-per-pixel style masks
or simply a wide enable).

## $328 – Sentinel / last-result

**Type:** long (written), word (read once at 0x23fba)  

Written as `$ffffffff` by the hunk-35 stub.  Sparse usage – possibly
a “no coordinate / invalid” sentinel or last-error flag.

## $112 – High-level state

**Type:** word  
Set to `3` immediately after the picture-wait function returns
(file 0x6d6).  Candidate for the game’s **screen / phase index**
(title → menu → flight, etc.).

## Port mapping

```cpp
// Approximate modern equivalents
uint16_t g_draw_mode;    // $36e
uint16_t g_pen;          // $324
uint32_t g_plane_mask;   // $b6
int32_t  g_sentinel;     // $328
uint16_t g_phase;        // $112
```

## Input variables

| Addr | Role |
|------|------|
| `$155` | current key code (see `docs/keyboard.md`) |
| `$206` | input-enable flag |
| `$207` | input mode / sub-state |
| `$2ac` | menu index |

## Blitter parameter scratch ($330–$33e)

Written by the coordinate-prep code immediately before the primary
line/fill entry (file 0x29a50–0x29a84):

| Addr | Type | Writer | Role |
|------|------|--------|------|
| `$330` | long | `move.l d1,$330` | intermediate (pointer / value) |
| `$334` | long | `move.l d1,$334` | intermediate |
| `$338` | long | `move.l d1,$338` | intermediate |
| `$33e` | word | `move.w d7,$33e` | size word → later BLTSIZE |

These are the last values computed from the geometric arguments before
the blitter registers are programmed.  Useful for dynamic tracing and
for the C++ `GfxState` mirror.

## View / camera translation ($b68 / $b6c / $b70)

Applied after the matrix multiply inside the transform routine:

| Addr  | Role |
|-------|------|
| `$b68` | translation X (andi `# $3fffff`) |
| `$b6c` | translation Y |
| `$b70` | translation Z (andi `# $3fffff`) |

```
object.x = transformed_x + ($b68 & $3fffff)
object.y = transformed_y + $b6c
object.z = transformed_z + ($b70 & $3fffff)
```

Results are written to the current object structure at offsets 0 / 4 / 8.

## Line geometry params ($350 / $352)

| Addr | Role |
|------|------|
| `$350` | paired with $352 (exact role open) |
| `$352` | screen Y (row index); multiplied by 40 for bitplane byte offset |

Filled at runtime before the hunk-35 line path runs.

## Line endpoints ($350 / $352)

| Addr | Role |
|------|------|
| `$350` | Y0 – line start Y |
| `$352` | Y1 – line end Y |

`height = (Y1 − Y0) + 1` feeds BLTSIZE bits 15–6 (`lsl #6`).

## Bitplane base ($b2)

Runtime pointer to the active bitplane; added to the Y×40 row offset
to form the BLTCPT destination address.

## Line parameter block ($34c–$352)

| Addr | Role |
|------|------|
| `$34c` | X-related (spilled d0) |
| `$34e` | X-related (spilled d1) |
| `$350` | Y0 – line start Y |
| `$352` | Y1 – line end Y |

Filled by `movem.w d0-d3, $34c` just before the blitter setup.

## Vertex buffer ($3000)

Runtime buffer of projected screen-space vertices (X,Y word pairs).
The line setup walks this buffer with `(a2)+`.

## $bb4 – object-loop counter / phase

Read and written inside the object loop (`$72dc`):

```
move.w $bb4, d0     ; file 0x76a4, 0x77c6
…
move.w d0, $bb4     ; file 0x77ce
```

Likely a per-frame or per-object phase counter.
