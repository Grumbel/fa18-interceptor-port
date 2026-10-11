# Picture-wait helpers: $ef4, $cce, and the caller

## Call graph

```
file 0x9d84 area
  jsr $84              ; setup / alloc?
  move.l $10 → args
  jsr $01c8            ; picture-wait function (file 0x4c8–0x6cc)
       │
       ├─ jsr $ef4     ; bounds-checked region helper
       ├─ jsr $996     ; structure walker (see func_996.md)
       ├─ jsr $cce     ; chunk-list advance
       └─ jsr $1c      ; BUTTONWAIT hook
  …
  move.w #3, $112      ; next function after picture-wait (0x6ce)
```

## Picture-wait function

| Item | Value |
|------|-------|
| Runtime | `$01c8` |
| File | `0x4c8` – `0x6cc` |
| Prologue | `link a6, #-82` |
| Sole caller | file `0x9d84` |

Caller pushes two longs from the structure at absolute `$10` before the call.

## $ef4 – bounds-checked consume / dispatch (file 0x11f4–0x127a)

```
link a6,#-4
if arg_size < 0: return -6
if remaining < needed: return -8
if size == 0: return (error path -9 / -3)
jsr $30                ; vector / library trampoline
if d0 == 0: return -9
if d0 == -1: return -3
; else advance structure pointers (+0xc, +0x20)
return status
```

Error codes: **-6**, **-8**, **-3**, **-9**.

Acts as a **safe “read/consume N bytes from a memory region”** helper
that optionally dispatches through `$30`.  Used from many sites
(0x538, 0x54e, 0x564, 0x596, 0x5ac, 0x680, 0xc4a, 0xe10, 0xe96).

## $cce – chunk-list advance (file 0xfce–0xffa)

```
link a6,#-4
a0 = first arg
if (a0) == null: return 0
; follow pointer at +0xc
; remaining -= chunk_size; total += chunk_size
; store next pointer back at +0xc
return 0
```

Very small.  Advances a linked list / chunk chain in the same structure
family that `$996` and `$ef4` operate on.

## Structure model (refined)

Across `$ef4`, `$996`, and `$cce` the in-memory object has at least:

| Offset | Role (inferred) |
|--------|-----------------|
| +0x00 | word fields (dimensions / counts) |
| +0x08 | type / flags |
| +0x0c | current pointer into data |
| +0x10 | size argument / limit |
| +0x18 | base or end pointer |
| +0x20 | accumulated / total size |

This is consistent with a **custom chunked binary resource** (not raw
IFF tags at this layer).  An earlier stage may have already decoded
ILBM or a packed asset into this descriptor.

## $30 note

`jsr $30` appears in `$ef4` and the picture-wait body.  Early init stores
the OpenLibrary result at `$30`, but the bytes at file 0x330 are not a
clean function entry.  Either:

1. A trampoline is written to `$30` later, or
2. `$30` is only used as a data slot and the absolute jsr sites are
   patched / redirected at runtime (WHDLoad does patch nearby vectors).

Needs dynamic confirmation.

## Next steps

1. Identify what `$84` (caller setup) allocates / returns.
2. Find who populates the structure at absolute `$10`.
3. Resolve the `$30` trampoline.
4. Map the structure fields more precisely once a real instance is seen
   (emulator memory dump or by tracing the asset-load path).

## $84 and $10 (caller setup)

### $84

Called from 16+ sites (empty-loop timing, picture-wait caller, blitter
region).  File offset 0x384 has no function prologue – `$84` is a
**runtime vector**, not a static function in the CODE image.

Likely a low-level service (AllocMem wrapper, WaitTOF, resource lock,
or similar) installed during startup.

### Structure pointer at $10

Caller at 0x9d78 does `move.l $10, a0` then pushes `(a0)` and `4(a0)`.

Known write:
```
0x825a  move.l #0x22, $10     ; constant, not a pointer
```

Other low-memory setup at the same site:
```
lea $62, a0 → $5a
move.l #0x8a, $5e
```

The pointer value used by picture-wait must be written elsewhere
(register-indirect store, or a later overwrite of `$10`).  Not yet
found as a direct `move.l ea, $10` of a code/data address.

## Sole caller of picture-wait `$01c8`

**Only one call site**: file `0x9d84`, inside the function starting at
file `0x9cca` (`link a6, #-2`, runtime `$99ca`).

### Sequence inside that function

```
… setup …
jsr $022c                 ; pre-check
move.l #-1, $10($8)       ; clear slots
… arithmetic on structure …
bset #1, $52b             ; set flag (same $52b tested by $64c!)

moveq #4, d0 / moveq #32
jsr $84                   ; resource/service
tst.l d0
beq  skip_picwait
  move.l $10, a0
  push 4(a0), (a0)
  jsr $01c8               ; PICTURE-WAIT
  move.l $10, a0
  clr.l  $30(a0)
  move.l #-1, $10(a0)

moveq #6, d0 / push $800
jsr $84
… jsr $02a6 …
```

### Connection to keyboard path

Both the picture-wait caller and `$64c` touch **`$52b`**:

- Picture-wait path: `bset #1, $52b` (sets bit 1)
- `$64c`: `btst #7, $52b` (tests bit 7)

Same flag byte, different bits — shared phase/state register for the
title / attract loop.
