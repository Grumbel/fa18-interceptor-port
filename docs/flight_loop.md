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

## Post-transform draw call convention (file 0x784c)

Called from the object loop with the iteration index on the stack:

```
; object loop (file 0x7818):
move.w -16(a6), d0        ; counter (0, 2, 4, …)
ext.l  d0
move.l d0, -(sp)
bsr    $784c              ; post-transform draw
```

Inside 0x784c the index lands at **10(a6)** and is forwarded to:

| Call | Argument | On return |
|------|----------|-----------|
| `jsr $c6`  | index (long) | `tst.l d0` – zero → skip branch |
| `jsr $68a` | index (long) | `tst.l d0` – zero → skip branch |
| `jsr $f4`  | (later) | |
| `jsr $88`  | (later) | |
| `jsr $0`   | (later) | |

`$c6` / `$68a` are runtime vectors (static image at those addresses is
unrelated code).  They return a status in d0; non-zero continues the
draw path (flag updates at object+$28, field manip at +$10/+30).

### Status

Without the runtime targets of `$c6`/`$68a`/`$12` the exact projection
and blitter invocation for object edges remains behind the vector
wall.  The recovered data flow is:

```
index → draw(0x784c) → $c6/$68a (status) → object field updates
                      → (vectors eventually reach mode $1b / blitter)
```

## Object-loop call sequence (refined)

```
$72dc object loop:
  …
  jsr $a0dc          ; object prep / flag test ($52a bit)
  jsr $a2f0          ; further object setup
  jsr $a0dc
  bsr $7ad8          ; transform: read $be6–$bf6 → $422/$426/$42a
  … unlk/rts into post-transform …
$784c post-transform:
  jsr $c6            ; status check
  jsr $68a           ; status check
  update object+40/+48/+50 counters
  jsr $f4 / $88      ; further vectors
```

`$a0dc` tests bit 0 of `$52a` and pointer at `$18`.
`$a2f0` tests pointer at `$28` and writes object fields.

## Post-transform routine (file 0x784c, static `$754c`)

Called after transform returns inside the object loop (path via flag
tests; not a direct fall-through from the `bsr $7ad8`).

```
link a6, #-18
movem.l a2, -(sp)
movea.l $c, a0
adda.w  #$26, a0          ; A0 → object+0x26
movea.l $c, a1
move.w  $28(a1), d0       ; state
move.l  a0, -4(a6)
tst.w   d0
ble     exit_path

move.l  a1, -10(a6)
addq.l  #4, a1
movea.l $c, a2
addq.l  #8, a2            ; A2 → object+8 (world Z area)
move.w  (a0), d0          ; type flags at +$26
move.l  a1, -14(a6)
move.l  a2, -18(a6)
btst    #1, d0
bne     alt_path

move.w  10(a6), d0        ; stack arg?
ext.l   d0
move.l  d0, -(sp)
jsr     $c6               ; ★ runtime vector (draw / project dispatch)
addq.l  #4, sp
tst.l   d0
beq     skip

; success path: clear some flag bits, set state=2
movea.l -4(a6), a0
move.w  (a0), d0
andi.w  #$fff7, d0
ori.w   #$0012, d0
move.w  d0, (a0)
movea.l $c, a0
move.w  #2, $28(a0)       ; state = 2
bra     more

; other paths set state=2, shift flags at +$10 into $4a0, etc.
```

### Role

1. Gate on object state (`+$28`) and type flags (`+$26`)
2. Call runtime vector **`$c6`** with a stack argument — this is the
   bridge toward projection / edge setup / draw
3. Update object state to 2 and tweak flag bits

`$c6` is installed dynamically (table `$151a`, index `$220`); static
image at `$c6` is unrelated.  Resolving its live target (when objects
are active) is the key remaining step for the post-transform → draw
chain.

### Object loop structure (refined)

```
$72dc object_loop:
  … init locals, tables $b8f/$bd1 …
  loop:
    resolve object → $c
    flag tests
    bsr  $77d8          ; transform (reads $be6 → $422/$426/$42a)
    … more flag tests …
    bsr  $754c          ; post-transform (file 0x784c) → jsr $c6
    next object
  unlk / rts
```
