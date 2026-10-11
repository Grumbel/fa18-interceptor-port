# WHDLoad Patch Sites Mapped to the Binary

The PL_ offsets from JOTD’s slave correspond directly to **file offsets**
in `assets/F-18_Interceptor_amiga` (confirmed for the protection site and
spot-checked for others).

## Protection

| File offset | Instruction / note |
|-------------|--------------------|
| `0x1ce70`   | `move.b #$ff, $27c` then `rts` – patched by slave to `#$01` |
| `$19737` / `$3a964` | protection-passed flags (data bytes set to 1 by slave) |

## Busy-wait / empty loop (`$8E2`)

```
0x08e2  jsr $f0
0x08e8  jsr $c8
0x08ee  jsr $84
0x08f4  jsr $14
0x08fa  jsr $84
0x0900  jsr $100
```

A sequence of absolute calls used as a timing / busy-wait loop.
The slave replaces the first instruction with a call to an emulator that
counts iterations so the game runs at a consistent speed on faster CPUs.

## Keyboard test (`$1590`)

```
0x1590  move.b $155, d0
0x1596  tst.b  d0
0x1598  beq    …
0x159a  tst.b  $206
…
```

Reads keyboard / input state from low absolute addresses.

## Speed regulation / VBL (`$8286`, `$92e6`)

- `$8286` – contains a `jsr $a8` and stack frame setup; patched to the
  slave’s speed-regulation routine.
- `$92e6` – short routine that ends with `rts`; used as a VBL hook.

## Picture wait (`$6b4`)

```
0x06b4  … 
0x06ba  jsr $1c
…
0x06cc  rts
```

Patched when BUTTONWAIT is set so the user can see the intro picture.

## Access-fault fixes

- `$fd7c` – data-looking region; slave writes a `jsr $100`.
- `$11468` – code that does pointer arithmetic with `$50` / `$80`.

## Usage for further RE

These sites are natural breakpoints / anchors:

1. Protection – skip or force the “passed” flags.
2. Empty-loop – identifies timing-sensitive code.
3. Keyboard – entry into the input path.
4. VBL / speed – main frame loop timing.
5. Picture wait – early title / splash display path.
