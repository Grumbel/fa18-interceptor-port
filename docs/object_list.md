# Object List / Iteration

## Object loop locals (file 0x75dc, rt `$72dc`)

| a6 offset | Init | Role |
|-----------|------|------|
| −1(a6)    | 0    | flag byte |
| −2(a6)    | 0    | flag byte |
| −3(a6)    | 0    | flag byte |
| −8(a6)    | `#$bd1` | table / structure A pointer |
| −12(a6)   | `#$b8f` | table / structure B pointer (used near transform) |
| −16(a6)   | 0, then `addq.w #2` each iter | **index / byte-offset counter** |
| −20(a6)   | from `$c` | **current object pointer** |

## Current object (`$c`)

- Written from a0 at file `0x762a` (`move.l a0, $c`)
- Read by the transform (`move.l $c, a0` then `adda.w #$26, a0`)
- Object structure offset **+$26** holds a word used for flags / type

## Iteration model

```
clr.w  index                    ; -16(a6)
loop:
  resolve object from tables at $b8f / $bd1 using index
  move.l object, $c
  … flag tests …
  bsr  transform ($77d8)        ; uses $c and stack vector
  addq.w #2, index
  bra  loop
```

`$b8f` and `$bd1` are absolute runtime addresses (low-memory data
slots).  The static image at the corresponding file offsets is code
that is either overwritten after init or lives in a shared address
space; treat them as abstract table bases for the port.

## Port mapping

```cpp
struct ObjectList {
  std::vector<Object> objects;
  size_t index = 0;

  void reset() { index = 0; }
  Object* next() {
    if (index >= objects.size()) return nullptr;
    return &objects[index++];
  }
};

// Per-frame (mirrors $72dc):
void object_loop(SDL_Renderer* ren, GfxState& gfx, ObjectList& list) {
  list.reset();
  while (auto* obj = list.next()) {
    draw_object(ren, gfx, *obj);   // transform → project → line
  }
}
```

## Object structure (expanded)

| Offset | Size | Role |
|--------|------|------|
| +0 | long | world X (after transform + view) |
| +4 | long | world Y |
| +8 | long | world Z |
| +16 ($10) | long | flags / type (bit tests; lsr → $4a0) |
| +40 ($28) | word | state (set to 2 in post-transform) |
| +48 ($30) | word | counter A (inc/dec, clamp 0..$7f) |
| +50 ($32) | word | counter B (inc/dec) |

Object pointer held in absolute `$c` during the frame loop.

## $bb4 – frame phase timer

```
move.w $bb4, d0
cmpi.w #1, d0
blt    skip
…
move.w $bb4, d0
subq.w #3, d0          ; decrement by 3
move.w d0, $bb4
bpl    ok
clr.w  $bb4            ; clamp at 0
```

Countdown timer used by the object loop; resets to 0 when exhausted.

## $496 – edge helper scratch

Three words (`movem.w d0-d2, $496`) used by the octant/clip helpers
in the vertex-walker CODE hunk.  Temporary edge parameters, not
persistent state.

## $4202 – edge / object descriptor

`lea $4202, a1` in the vertex walker.  Fields read at +6, +8, +$a
for clip bounds.  Layout still partially open; may be a static
viewport or active-object geometry header.
