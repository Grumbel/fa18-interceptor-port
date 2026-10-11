# Function $996 (file 0x0c96) – picture-wait private helper

## Overview

- **Runtime**: `$0996`
- **File**: `0x0c96`
- **Prologue**: `link a6, #-116` + save d2
- **Only caller**: picture-wait path at file `0x06a0`
- **Role (inferred)**: Validate / parse a structured binary object
  (likely an image or chunk header) held in memory, then invoke `$ef4`.

## Control flow (simplified)

```
link a6,#-116
save d2

; --- field extraction / packing ---
; reads structure via a0 (arg), offsets 0, 2, 8, 0xa, 0xc, …
; masks with 0x0F and 0x7F  → packed bitfields
; stores locals at negative a6 offsets

cmpi  field, #1
bls   ok1
moveq #-6, d0          ; error
rts

; size / bound checks
cmp   …
b*    error → return -6

; loop with counter, compares against 0x11 (17)
loop:
  …
  cmpi.l #0x11, d0
  bge   done_loop
  …
  bra   loop

; later:
jsr $ef4               ; shared helper
tst.l d0
beq   continue
restore / unlk / rts   ; propagate error

; … more processing (function continues past 0xe2a)
```

## Error convention

Returns **`-6`** in d0 on validation failure (several early-exit paths).

## Structure offsets observed

| Offset | Access        | Notes                          |
|--------|---------------|--------------------------------|
| +0     | word          | dimension / count              |
| +2     | word          | related size                   |
| +5     | byte          |                                |
| +8     | byte/word     | flags / type                   |
| +9     | byte          | compared after subq            |
| +0xa   | byte          |                                |
| +0xc   | long/pointer  | base for further reads         |

Bit masking with `0x0F` and `0x7F` suggests packed header fields
(common in Amiga IFF/ILBM and custom game formats).

## Relationship to picture-wait

```
picture-wait function
  jsr $ef4     ; pre-check
  jsr $996     ; THIS – parse/validate structure
  jsr $cce     ; pointer adjust
  jsr $1c      ; BUTTONWAIT
```

`$996` is the only non-trivial private routine on this path and is the
best static lead for title/splash asset handling.

## Next RE steps for this function

1. Continue disassembly from 0xe2a to the true function end.
2. Identify the caller-supplied structure (argument at 8(a6) / 0x14(a6)).
3. Match field layout against ILBM BMHD or a custom game header.
4. Determine whether pixel data is decoded here or only metadata.
