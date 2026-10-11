# Load Model

## Hypothesis (confirmed for main CODE segment)

The first large `HUNK_CODE` payload (file offset **0x300**, size ~41 KB) is
loaded at **runtime address 0**.

```
file_offset = 0x300 + runtime_address
```

### Evidence

1. WHDLoad PL_ offsets match file offsets for patches inside the code body
   (e.g. protection at 0x1ce70).
2. Absolute targets used by picture-wait resolve to proper function
   prologues:

| Runtime | File     | Prologue              |
|---------|----------|-----------------------|
| `$0ef4` | `0x11f4` | `link a6, #-4`        |
| `$0996` | `0x0c96` | `link a6, #-116`      |
| `$0cce` | `0x0fce` | `link a6, #-4`        |

3. Early-init stores to `$30`, `$40`, `$44`, etc. write into the same
   address space (data slots embedded at the low end of the segment or
   overlaid variables).

Very low targets (`$14`, `$1c`, `$84`, …) land inside the early-init
sequence itself; some may be mid-function entry points, timing helpers,
or data rather than true function starts.  Higher targets (≥ ~$200)
consistently look like real functions.

## Picture-wait call chain (resolved)

```
picture-wait region (file 0x0680)
  jsr $ef4   → file 0x11f4   (link a6,#-4)   ; helper / check
  jsr $996   → file 0x0c96   (link a6,#-116) ; larger routine (stack frame 116 bytes)
  jsr $cce   → file 0x0fce   (link a6,#-4)   ; short helper, ends with unlk/rts
  jsr $1c    → (low; BUTTONWAIT patch point)
  …
  move.w #3, $112
  rts
```

## Usage

When analysing any `jsr $xxxx.L` with `xxxx < ~0xA000`:

```
file_offset ≈ 0x300 + xxxx
```

This turns the absolute call graph into ordinary static analysis.

## Picture-wait function structure (file ~0x5e0–0x6cc)

The BUTTONWAIT patch site (`0x6b4`) sits inside a larger function that
ends at `0x6cc` (`unlk a6 / rts`).  Outline:

```
… (function body, uses jsr $30, jsr $a8, bit packing)
0x680  jsr $ef4          ; widely-used helper (also called from 0x538…)
0x6a0  jsr $996          ; only called from here – image/structure parser?
0x6ae  jsr $cce          ; only called from here – pointer adjust helper
0x6ba  jsr $1c           ; BUTTONWAIT hook (WHDLoad patches here)
0x6ca  unlk a6
0x6cc  rts

0x6ce  link a6,#-24      ; NEXT function
0x6d6  move.w #3, $112   ; state / mode variable
0x6de  moveq #29, d0
…
```

`$ef4` is a general-purpose helper (many call sites).  
`$996` and `$cce` are private to this path – strong candidates for
title/splash asset preparation.

`$30` is called as `jsr $30` from this function; under the load model
that is file 0x330 (inside early init) unless the slot was later
overwritten with a different function pointer.

## $996 summary

Private picture-wait helper (file 0x0c96).  Parses/validates a packed
binary structure (error code -6), loops over a count of 17, then calls
`$ef4`.  See `docs/func_996.md`.
