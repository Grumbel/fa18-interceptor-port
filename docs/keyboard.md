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
