# Main Flight / Frame Loop

## Top-level frame function

| Item | Value |
|------|-------|
| File | `0x1424` – `0x1812` |
| Runtime | `$1124` |
| Frame size | `link a6, #-2` |

### Outline

```
$1124 (file 0x1424)
  … early setup …
  jsr $84c / $85e / $0 …          ; service helpers
  tst.b $17d
  beq  skip1
  jsr $72dc                       ; ★ object transform/draw loop
skip1:
  jsr $0
  …
  jsr $72dc                       ; ★ (second site)
  jsr $9f4
  …
  jsr $72dc                       ; ★ (third site)
  jsr $3d4c / $4aa0               ; post-draw helpers
  …
  ; --- keyboard / mode dispatch (already documented) ---
  move.b $155, d0
  tst.b  d0
  beq    …
  tst.b  $206
  …
  jsr $5b6 / $64c / $878          ; mode handlers
  …
  unlk / rts
```

Three conditional calls to the object loop allow different phases
(attract, flight, pause, …) to share the same transform path.

## Object transform / draw loop

| Item | Value |
|------|-------|
| File | `0x75dc` – `0x784a` |
| Runtime | `$72dc` |
| Frame size | `link a6, #-22` |

```
$72dc
  link a6,#-22
  jsr $0                          ; init / clear?
  ┌─ loop head (file 0x7612) ─────────────────────────┐
  │  flag tests: $18d, $17e, $25b, $17d, $52b, $155     │
  │  optional jsr $a0dc / $a2f0                         │
  │  bsr  $77d8          ; ★ matrix transform (0x7ad8)  │
  │  … post-transform work …                            │
  │  bra  loop head                                     │
  └─────────────────────────────────────────────────────┘
  unlk / rts
```

### Call graph (flight frame)

```
frame $1124
  ├─ helpers ($84c, $85e, …)
  ├─ object loop $72dc          (×1–3 depending on flags)
  │    └─ transform $77d8
  │         └─ matrix muls → $422/$426/$42a
  │    └─ (geometry then reaches clamp + blitter line)
  ├─ post-draw ($9f4, $3d4c, $4aa0)
  └─ keyboard dispatch
       ├─ $5b6  frame/service tick
       ├─ $64c  title/proceed → action 35/36
       └─ $878  resource setup
```

## Connection to blitter

The transform results (`$422/$426/$42a`) feed the geometry-prep code
that writes `$330/$334/$338/$33e` and then programs the blitter
(BLTCON0=`$09f0` at file `0x29a88`).  The exact glue between the
object loop and the overlay blitter entry is still being traced
(likely a vector or overlay call after the transform).

## Port mapping

```cpp
void frame() {
  // service helpers …
  if (need_objects)
    object_loop();          // $72dc
  // post-draw …
  handle_input();           // $155 / $5b6 / $64c / $878
}

void object_loop() {
  for (each object) {
    transform(object);      // $77d8 → Mat3 * Vec3
    // project + clamp + draw_line  (blitter path)
  }
}
```

## Open

- Precise object-list walk (where the “next object” pointer lives).
- How transform results reach the blitter overlay (vector? direct call?).
- Perspective divide location.
