# Keyboard Input Path

## Key input byte: `$155`

```
move.b $155, d0     ; read current key
```

**12 call sites**, including the central handler at file **0x1590**
(WHDLoad PL_L patch point).

`$155` holds the most recent key code (ASCII or raw, depending on the
preceding input routine).  A zero value means “no key”.

## Central handler (file 0x1590)

```
0x1590  move.b  $155, d0
        tst.b   d0
        beq     no_key
        tst.b   $206            ; input-enable flag
        beq     alt_path

0x15a4  move.w  $2ac, d0        ; secondary state / menu index
        … index arithmetic …
        cmpi.b  #'0', d0        ; digit / menu-item check

0x15cc  jsr $a4                 ; vector chain (runtime-installed)
        jsr $2f6
        jsr $fe
        jsr $64
        jsr $2
        jsr $8b8

0x15f0  move.b  $207, d0        ; mode / sub-state
        … cmpi #1 / #2 …
        jsr $5b6 / $64c / $878  ; mode-specific handlers
```

## Related low-memory

| Addr | Role |
|------|------|
| `$155` | current key code |
| `$206` | input-enable flag |
| `$207` | mode / sub-state |
| `$2ac` | menu index / secondary state |

## Comparison values observed

- ASCII `'0'` (`$30`) – digit / menu item
- `#1`, `#2` – mode selectors
- `#16` (`$10`) – possible special key / function

## Port mapping

```cpp
uint8_t  key_code;      // $155
bool     input_enabled; // $206
uint8_t  input_mode;    // $207
uint16_t menu_index;    // $2ac
```

SDL2 key events should update `key_code` and drive the same state
transitions once the handler logic is fully reverse-engineered.

## Mode handlers called from 0x1590

### `$5b6` (file 0x8b6) – frame / service tick

```
moveq  #32, d0
move.l $4a, a0          ; structure pointer
… push args …
jsr    $0               ; runtime vector
cmpi.w #$c560, $1c      ; timer / frame threshold?
jsr    $f0 / $c8 / $84 / $14 / $84 / $100   ; service vector chain
```

Looks like a **periodic service / frame-advance** handler rather than
pure key decode.  Reuses the same `$84` timing vector seen elsewhere.

### `$64c` (file 0x94c) – conditional path into picture-wait area

```
… same $4a / jsr $0 setup …
btst   #7, $52b         ; flag test
beq    alt
moveq  #50, d0
jsr    $9d7a            ; near picture-wait caller (0x9d84)
…
jsr    $dc
tst.l  $c2
```

Can divert into the **picture-wait / title path** region.  Likely the
handler for “key pressed while on title / attract → proceed”.

### `$878` (file 0xb78) – resource / pointer setup

```
… pointer copy into $ae …
jsr    $4e
jsr    $30              ; library-base style call
move.l d0, $b2
… another jsr $30 …
move.l d0, $16a4
rts
```

Allocates or initialises resources; stores results at `$b2` and `$16a4`.

## Dispatch summary

```
key available ($155 ≠ 0) and input enabled ($206 ≠ 0)
  ├─ mode check #1 → jsr $5b6   (frame/service)
  ├─ mode check #2 → jsr $64c   (title/proceed → picture-wait area)
  └─             → jsr $878   (resource setup)
```

## Corrected $64c → title-transition chain

Earlier note that `$64c` calls into the picture-wait **caller** was based
on a file/runtime mix-up.  Correct chain under the load model
(`file = 0x300 + runtime`):

```
$64c (file 0x94c)
  btst #7, $52b
  moveq #50, d0
  jsr  $9d7a              ; runtime $9d7a = file 0xa07a
        │
        ├─ btst #2, $52a
        ├─ moveq #35, d0  ; action index
        │  bsr  file 0x9f7c
        └─ moveq #36, d0  ; action index
           bsr  file 0x9f7c
```

### `$9d7a` (file 0xa07a) – dual action trigger

```
link a6,#0
btst #2, $52a
beq  done
push arg
moveq #35, d0
bsr  0x9f7c
push arg
moveq #1, d0
moveq #36, d0
bsr  0x9f7c
done: unlk / rts
```

### `0x9f7c` – table-driven action dispatcher

```
link a6,#0
d0 = action index (35 or 36)
shift / index into table at absolute 0
if entry null: return
… pointer setup from table entry …
```

Action indices **35** and **36** select entries in a global table
(base address 0 – likely filled during init).  These are distinct from
the picture-wait function (`$01c8` / file 0x4c8) but sit in the same
overall title / attract-mode flow.

### Shared flags

| Addr | Role |
|------|------|
| `$52a` | bit 2 tested by `$9d7a` |
| `$52b` | bit 7 tested by `$64c` |

### Picture-wait caller (unchanged)

Still at **file 0x9d84** (`jsr $01c8`).  Not directly called from `$64c`;
reached via a different path (possibly the `$84` setup sequence at
file 0x9d6c, or another caller of `$01c8`).

## Action table (absolute base 0)

Dispatcher at file `0x9f7c`:

```
d0 = action index
asl.l #2, d0          ; ×4
lea   0.l, a0
adda.l d0, a0         ; a0 → table[index]
tst.l (a0)
beq   skip
… setup …
jsr   $78             ; runtime vector
```

| Index | Offset | Notes |
|-------|--------|-------|
| 35 | `$8c` | triggered by `$9d7a` |
| 36 | `$90` | triggered by `$9d7a` |

Table slots live in the low-memory vector/data area and are **filled at
runtime** (no static initialisers found for `$8c`/`$90`).  Entry 0 is
written at file `0xac0` (`move.l d0, $0`).

## Action table population (overlay)

No static initialisers write `$8c`/`$90`.  Population is performed by
functions living in a small overlay CODE hunk:

| File offset | Size | Notes |
|-------------|------|-------|
| `0x4bf0c`   | 1180 B | HUNK_CODE containing slot create / clear |

### Slot-create / init (runtime entry ≈ `$4bffa` relative to overlay load)

```
d0 = action index (from stack)
asl.l #2, d0
lea   0.l, a0
adda.l d0, a0          ; a0 = &table[index]
move.l (a0), a1        ; existing?
… jsr $30 (AllocMem-style) …
move.l d0, (a0)        ; store new structure pointer
; then initialise structure fields:
;   +4  ← arg
;   +8  ← #$01660000
;   +c  ← #$003f0000
;  +10  ← #1
;  +20  ← 0
…
```

### Slot-clear (file ≈ `0x4c0a2`)

```
asl.l #2, d0
… a0 = &table[index]
suba.l a1, a1
move.l a1, (a0)        ; null the slot
```

### Structure layout (from init stores)

| Offset | Value written | Likely meaning          |
|--------|---------------|-------------------------|
| +0     | (pointer)     | self / next?            |
| +4     | caller arg    |                          |
| +8     | `$01660000`  | packed / fixed-point?   |
| +c     | `$003f0000`  |                          |
| +10    | 1             | active / refcount       |
| +20    | 0             |                          |

The dispatcher at `0x9f7c` later reads the pointer, scales an argument
by 16, and stores into the structure before calling vector `$78`.

**Open:** which call site(s) invoke the create function with indices 35
and 36 (title-transition actions).  Likely from overlay load / mode
entry code that is not yet mapped.  The primary-segment callers of the
dispatcher (`$9d7a`, `0xa046`, `0xa08e`) only *trigger* already-filled
slots.

## Action slots $8c / $90 (indices 35 / 36)

| Addr | Index | Notes |
|------|-------|-------|
| `$8c` | 35 | `35 × 4 = $8c` |
| `$90` | 36 | `36 × 4 = $90` |

Created during early init (file 0x9960, called from 0x756):
1. `jsr $1a6` creates slot 35
2. `jsr $22c` creates slot 36 (paired with 35)
3. Fields +8 / +16 customised; bit 2 of `$52a` set

See `docs/action_create.md` for the full create path.
