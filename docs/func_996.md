# Function $996 (file 0x0c96–0x0ef6) – picture-wait private helper

## Overview

| Item | Value |
|------|-------|
| Runtime | `$0996` |
| File range | `0x0c96` – `0x0ef6` (~0x260 bytes) |
| Prologue | `link a6, #-116` + save d2 |
| Epilogue | restore d2 / `unlk a6` / `rts` |
| Only caller | picture-wait path at file `0x06a0` |

## Role (inferred)

Validate and walk a packed binary structure in memory (likely image /
chunk metadata), performing bound checks and pointer arithmetic, then
invoking the shared helper `$ef4`.  On failure returns a negative error
code in d0.

## Error codes

| d0 | Meaning (inferred) |
|----|--------------------|
| `-6` | Validation failure (field bounds / type checks) |
| `-7` | Secondary failure path (after `jsr $0`) |
| `0`  | Success |

## Calls made

| Target | Notes |
|--------|-------|
| `$ef4` | Shared helper – called multiple times (0xe10, 0xe96) |
| `$0000` | `jsr $0` at 0xe58 and 0xec6 – may be a vector installed at the start of the segment after init |

## Control-flow outline

```
0xc96  link a6,#-116 / save d2
       extract packed fields (masks 0x0F, 0x7F) from structure
       cmp type-field, #1 → else return -6
       size / bound checks → else return -6

0xd3e  loop over elements (counter vs 0x11 = 17)
       …

0xdc8  outer loop
         …
         jsr $ef4
         if d0 != 0: return d0
         …
         jsr $0          ; vector / helper
         …
         jsr $ef4
         if d0 != 0: return d0
         …
         jsr $0
         if failed: return -7
       loop back

0xeee  moveq #0, d0      ; success
0xef0  restore / unlk / rts
```

## Structure offsets observed

| Offset | Access | Notes |
|--------|--------|-------|
| +0 | word | dimension / count |
| +2 | word | related size |
| +5 | byte | |
| +8 | byte/word | flags / type (must be ≤ 1 for one check) |
| +9 | byte | post-decrement compare |
| +0xa | byte | |
| +0xc | long/pointer | base for further reads |
| +0x18 | long | pointer arithmetic |
| +0x20 | long | size compare |

Bit masking with `0x0F` / `0x7F` indicates packed header fields.

## Comparison with on-disk ILBM

On-disk assets (`pix/frnt5`, `pix/inst5`) are standard IFF ILBM:

- BMHD: 288×12 and 320×55, 5 planes, mask=2, compression=1 (ByteRun1)
- CMAP: 96 bytes (32 colours)
- BODY: compressed planar data

`$996` does **not** obviously parse a `"FORM"`/`"BMHD"` tag sequence; it
works on an already-in-memory structure with packed bitfields.  The
structure may be a **pre-parsed** or **custom game-internal** descriptor
built from ILBM data earlier, or a completely custom format for the
title graphics stored inside the executable.

## Next steps

1. Identify the caller-supplied argument (structure pointer) – who builds it?
2. Trace what `$ef4` does when called from this context.
3. Resolve what is installed at `$0000` after startup.
4. Dynamic trace under an emulator remains the fastest way to see the
   actual memory contents of the structure.
