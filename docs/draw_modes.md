# Draw Mode Dispatcher

## Pattern

Almost every graphics primitive in the binary follows:

```
move.w #MODE, $36e.L
jsr    $12.L              ; runtime draw dispatcher
```

`$12` is a **runtime-installed vector** (static image at file 0x312 is
early-init code, overwritten after startup).  The dispatcher reads
`$36e` and jumps to the matching primitive.

## Known modes

| Mode | Hex | Sites | Notes |
|------|-----|-------|-------|
| 1–10 | `$01`–`$0a` | 1 each | sequential small primitives |
| 12–18 | `$0c`–`$12` | 1 each | |
| 20–23 | `$14`–`$17` | 1 each | |
| **27** | **`$1b`** | **1** | **line draw – hunk 35 entry** |
| 28 | `$1c` | 1 | |
| 30–32 | `$1e`–`$20` | 1 each | |
| 34 | `$22` | 1 | |
| 38–41 | `$26`–`$29` | 1 each | |
| 48 | `$30` | 1 | |
| 51–56 | `$33`–`$38` | 1 each | |
| 58–66 | `$3a`–`$42` | 1 each | |
| 70 | `$46` | 1 | |

45 distinct modes total.  Mode `$1b` is the only one whose body has
been fully identified (hunk 35: clamp + blitter line setup).

## Connection to the flight path

```
frame $1124
  → object loop $72dc
      → transform $77d8  (matrix + view translation → object+0/+4/+8)
      → draw path 0x784c
          → jsr $c6 / $68a / $f4 / $88 / $0   (low vectors)
  → post-draw helpers

Elsewhere (overlays / mode-specific code):
  move.w #$1b, $36e
  jsr $12                → hunk-35 line primitive
```

The exact site that issues `move.w #$1b, $36e` / `jsr $12` for
projected geometry is inside an overlay (file 0x28274 is the *body*
of mode `$1b`, not a caller).  Callers live in other overlay hunks
that set the mode and invoke the dispatcher.

## Port mapping

```cpp
enum DrawMode : uint16_t {
  // … many …
  Line = 0x1b,
};

void dispatch_draw(GfxState& gfx, DrawMode mode) {
  gfx.draw_mode = mode;
  switch (mode) {
    case DrawMode::Line: /* clamp + SDL_RenderDrawLine */ break;
    // …
  }
}
```

## Open

- Body of `$12` after runtime install (likely a jump table on `$36e`)
- Which overlay site draws projected object edges with mode `$1b`
- Bodies of the other 44 modes

## Mode $01 – geometry primitive with perspective/interpolation

Issued by function at file **0x1c1f4**:

```
muls.w / divs.w          ; fixed-point (a*b)/c
move.w #$1, $36e
jsr    $12
```

Uses buffers at `$3c00`, `$3600` and low addresses `$1a2`/`$198`/`$fc`
(via lea in the function prologue).  No absolute callers found —
function lives in an overlay and is called relatively from within
that overlay.  Likely edge-setup or span-fill rather than pure line
draw (mode `$1b`).

## Mode $1b sole issuer

**Only one site** in the entire binary issues mode `$1b`:

```
; file 0x28274 – start of HUNK_CODE (size 0x205 longwords)
move.w #$1b, $36e.L
jsr    $12.L
moveq  #0, d0
rts
```

This is a thin wrapper: force line mode, call the draw engine at `$12`.
It lives in its own CODE hunk (header at 0x2826c).

### Companion mode in same hunk

| Mode | File | Notes |
|------|------|-------|
| `$1b` (27) | 0x28274 | line |
| `$16` (22) | 0x28698 | same hunk |

### How projected geometry reaches the line drawer

The flight-frame path does **not** call 0x28274 directly (no absolute
callers).  Instead:

1. Transform writes world coords to object+0/+4/+8
2. Post-transform (0x784c) calls `$c6` / `$68a` with the object pointer
3. Somewhere in the overlay chain, screen-space vertices land in `$3000`
4. Line setup reads `$3000`, fills `$34c`–`$352`, programs the blitter
5. Mode `$1b` wrapper (or the blitter path directly) is reached via `$12`

Exact connection from step 2 → 4 still needs the overlay load map or a
dynamic trace.
