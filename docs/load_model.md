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
