# 3D Transform / Projection Candidate

## Primary matrix multiply (file 0x7ad8, runtime `$77d8`)

```
link a6, #-10
movem …
move.l $c.L, a0          ; structure / object base
…
; coefficient table at absolute $0be6–$0bf6 (file ~0xee6)
muls.w  …                ; 9+ multiplies
add.l
asr.l  #6, …             ; fixed-point scale
move.l d0, $422          ; result X?
move.l d2, $426          ; result Y?
move.l d1, $42a          ; result Z?
…
unlk / rts               ; ends ~0x7d26
```

### Coefficient / result addresses

| Runtime | File   | Role (inferred) |
|---------|--------|-----------------|
| `$0be6`–`$0bf6` | `0xee6`+ | 3×3 (or 3×4) matrix elements |
| `$422`  | —      | transformed X |
| `$426`  | —      | transformed Y |
| `$42a`  | —      | transformed Z |
| `$c`    | —      | current object / view structure |

### Caller

Single static caller found:

```
file 0x77f6  bsr.w  $7ad8
```

This sits inside a loop that iterates objects/vertices (branch-back at
`0x7828 → 0x7612` region).  The surrounding function begins at least as
early as file `0x75dc` (`link a6,#-22`) and performs flag tests on
`$52b`, `$155`, `$25b`, `$17d` before entering the transform path.

## Secondary multiply cluster (file 0x4a5c)

Smaller set of `muls.w` + stores to `$534` / `$49c`.  Possibly a
simpler 2-D scale or HUD projection helper.

## Implications for the port

1. The recovered matrix multiply is the natural place to inject a modern
   4×4 (or 3×4) transform.
2. Results land in low-memory slots that the later clamp + blitter line
   path already consumes (via the geometry prep that feeds `$330`–`$33e`).
3. Once the loop that calls `$77d8` is fully outlined, the flight-frame
   sequence becomes:

   ```
   for each object/vertex:
     transform ($77d8)
     project / clamp (hunk 35)
     blitter line/fill (0x29a88)
   ```

## Open

- Exact layout of the matrix at `$0be6` (row-major? fixed-point format).
- Perspective divide location (only one `divs` found in primary; may live
  in an overlay or be done with reciprocal multiply).
- Full bounds of the outer per-frame loop.
