# Function `$c6` — object-indexed post-transform worker

## Resolution

The static absolute `jsr $c6.L` in primary CODE is a **relocatable
reference to offset `$C6` inside a separate CODE hunk**, not an
indirect jump through a low-memory vector slot.

| Item | Value |
|------|-------|
| On-disk hunk header | file `0x1E9FC` |
| Hunk payload start | file `0x1EA04` (size 2244 bytes) |
| Function offset in hunk | **`$C6`** |
| File offset of function | `0x1EACA` |
| Live address (this session) | **`$00C25206`** (hunk base `$00C25140`) |
| Call site in primary | file `0x759A` (`jsr $c6.L` → relocated to `$00C25206`) |

The dynamic `move.l d6,$c6` installs documented in `draw_modes.md`
(overlay at file `0x12664`) write to a *different* slot (live
`$00C16E26` this session). That slot held `$00C03D46` in the in-flight
dump and is a separate mechanism (sibling of `$be`).

## Calling convention

```
; caller (primary, rt ~$729a region):
move.w  10(a6), d0        ; object index (word)
ext.l   d0
move.l  d0, -(sp)         ; push as long
jsr     $c6.L             ; → object worker
addq.l  #4, sp
tst.l   d0                ; status: 0 = skip / fail
beq.s   ...
```

- **Input:** object index (0..19) on stack
- **Output:** d0 status (0 = early-out or inactive)
- **Side effects:** reads/writes the indexed object in the pool

## Prologue (live)

```asm
$00C25206  moveq   #0, d0
$00C25208  move.b  $00C444F5, d1    ; gate flag A (was $18d)
$00C2520E  or.b    $00C444E6, d1    ; gate flag B (was $17e)
$00C25214  bne.s   <early rts>      ; both flags clear required
$00C25216  link    a6, #-$5C        ; large frame
$00C2521A  move.l  8(a6), d0        ; arg0 = index
$00C2521E  lsl.w   #6, d0           ; ×64 = object stride
$00C25220  lea     $00C449AA, a3    ; object pool base (was #$642)
$00C25226  adda.w  d0, a3           ; a3 = &pool[index]
$00C25228  movem.w 12(a3), d0-d2    ; load object+12..+16 words
; ... continues with object field processing, matrix-related
;     stores, and further pool access
```

Key points:

1. **Gated** by two flag bytes (`$00C444F5`, `$00C444E6`). Both must
   be zero or the function returns immediately with d0=0.
2. **Index → pointer** via `index * 64 + pool_base`, confirming the
   64-byte object stride and pool base `$00C449AA`.
3. Large stack frame (`-$5C` = 92 bytes) — substantial local state.
4. Subsequent body reads more object fields and writes results back
   into structures near `$00C44EB0` / `$00C44EBC` (scratch / output).

## Relationship to post-transform path

The post-transform routine at primary rt `$784c` (file `0x7B4C`) does
**not** directly `jsr $c6`. It sets object state fields and reads the
matrix. The `jsr $c6` call site at file `0x759A` sits earlier in the
object-loop / draw path (near the `$72dc` object iteration region).

Flow (updated):

```
object loop ($72dc)
  … resolve index …
  jsr $c6          → per-object worker (this function)
  … on success …
  bsr transform    ($77d8)
  bsr post-xform   ($784c)   ; sets state=2, etc.
  … draw via $12 / geometry …
```

## On-disk symbols used (before reloc)

| Static | Live (session) | Role |
|--------|----------------|------|
| `$018D` | `$00C444F5` | gate flag A |
| `$017E` | `$00C444E6` | gate flag B |
| `$0642` | `$00C449AA` | object pool |
| `$0B48` | `$00C44EB0` | scratch / out |
| `$0B50` | `$00C44EB8` | scratch / out |
| `$0B42` | `$00C44EAA` | scratch / out |
| `$0B54` | `$00C44EBC` | scratch / out |
| `$0388` | (cleared) | local counter / mode |

## Status

- [x] Hunk + file offset identified
- [x] Live address confirmed in in-flight dump
- [x] Calling convention and index×64 pool math confirmed
- [x] Gate flags identified
- [ ] Full body decompilation (remaining ~2 KB of hunk)
- [ ] Exact meaning of gate flags `$18d`/`$17e`
- [ ] Distinguish from the *vector-slot* `$c6` written by overlay install
