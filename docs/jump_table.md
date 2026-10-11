# Low-Memory Jump Table / Vector Architecture

## Summary

F/A-18 Interceptor installs a large **jump/vector table in low absolute
memory** (addresses below ~$1000).  Nearly all inter-routine calls use

```
jsr  $xxxx.L          ; absolute long
```

with targets in that range.  There are **~197 unique targets < $1000**.

This is why:

- Classic PC-relative or relocatable call patterns are rare.
- String / data cross-references cannot be found by searching for file
  offsets as immediates – the runtime addresses depend on where the
  vectors point after initialisation.
- The early-init code (file 0x300+) writes key bases into low memory
  (`$30` = library base after OpenLibrary, `$40`/`$44` = entry params,
  `$2c`/`$3c` = saved a6/a7, etc.).

## Notable low-memory slots (from early stores)

| Address | Written from (file) | Likely role                          |
|---------|---------------------|--------------------------------------|
| `$04`   | 0x034a (many times) | Often overwritten (custom ExecBase?) |
| `$2c`   | 0x031a              | Saved a6                             |
| `$30`   | 0x04bc              | Library base (OpenLibrary result)    |
| `$3c`   | 0x0320              | Saved a7 (stack)                     |
| `$40`   | 0x0300              | Entry d0                             |
| `$44`   | 0x0306              | Entry a0                             |
| `$4c`   | 0x0380              | Saved a1                             |

## Notable call targets used by known sites

| Target | Used by (file)        | Context                |
|--------|-----------------------|------------------------|
| `$1c`  | picture-wait 0x06ba   | BUTTONWAIT hook point  |
| `$14`  | empty-loop 0x08f4     | timing busy-wait       |
| `$84`  | empty-loop (×2)       | timing busy-wait       |
| `$c8`  | empty-loop 0x08e8     | timing busy-wait       |
| `$f0`  | empty-loop 0x08e2     | timing busy-wait       |
| `$100` | empty-loop 0x0900     | timing busy-wait       |
| `$a8`  | speed-reg 0x8290      | frame timing           |
| `$18c` | vbl_hook 0x92f2       | VBL related            |
| `$ef4` | picture-wait 0x0680   | early display path     |
| `$996` | picture-wait 0x06a0   | early display path     |
| `$cce` | picture-wait 0x06ae   | early display path     |

## Implications for RE and the port

1. **Static tracing** of control flow requires first discovering what
   each vector points to after the jump table is built (dynamic tracing
   under an emulator, or finding the table-construction code).
2. **WHDLoad PL_ offsets** that landed on real code (protection, keyboard,
   etc.) are patches *inside* the loaded segment body, not inside the
   low-memory vector table.
3. For the C++ port we can treat the vector table as an abstract set of
   function pointers / services and reimplement them directly rather
   than reproducing the low-memory layout.

## Next steps

- Locate the code that *fills* the jump table (likely shortly after
  OpenLibrary / file loading).
- Once a few vectors are resolved, the picture-wait and empty-loop
  paths become fully traceable.

## Update: load-base resolution

The “jump table” interpretation is partially refined.  Many absolute
targets are simply **offsets within the first CODE segment loaded at
runtime address 0** (see `docs/load_model.md`).

```
file_offset = 0x300 + runtime_address
```

Targets such as `$ef4`, `$996`, `$cce` are ordinary functions inside that
segment.  Very low addresses may still be special (data, mid-function
entry, or separately installed vectors).
