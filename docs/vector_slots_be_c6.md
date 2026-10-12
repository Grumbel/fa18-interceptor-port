# Vector slots `$be` / `$c6` — mode-selected data context pointers

## Distinction from the `$c6` function

| Name | Kind | Live (session) | Role |
|------|------|----------------|------|
| **`$c6` function** | CODE (relocatable offset `$C6` in hunk) | `$00C25206` | Object motion step (`docs/func_c6.md`) |
| **`$c6` vector slot** | Data pointer at abs `$c6` | slot `$00C16E26` → target `$00C03D46` | Mode-selected context block |
| **`$be` vector slot** | Data pointer at abs `$be` | slot `$00C16E1E` → target `$00C01550` | Sibling context block |

These are **not** function pointers. Both targets begin with null words and
contain mixed pointers + constants — scene / draw context structures.

## Installation (overlay file `0x125D4`–`0x12668`)

```asm
; --- $be ---
lea     $151a.L, a1         ; selection table
move.b  $220.L, d7          ; current mode index
ext.w   d7
asl.w   #2, d7              ; ×4
move.w  (0,a1,d7.w), d7     ; table word
ext.l   d7
asl.l   #2, d7
; d6 = base adjusted by d4/d2 …
move.l  d6, $be.L

; --- $c6 (sibling, table entry +2 / different regs) ---
lea     $151a.L, a1
move.b  $220.L, d7
…
move.l  d6, $c6.L
```

After install the code may `jsr $12` (draw-mode dispatcher) and test
flag bits on `$22a`.

The install overlay was **not resident** in the in-flight dump; the
slots already held the computed pointers from an earlier mode switch.

## Target structure snapshots (in-flight)

### `$be` → `$00C01550`

| Off | Value | Notes |
|-----|-------|-------|
| +00 | 0 | |
| +04 | 0 | |
| +08 | `$00C16D82` | pointer (near `$c` / object state) |
| +0C | `$00C055F8` | pointer |
| +10 | `$00C056E2` | pointer |
| +14 | `$0000A400` | constant (screen-bias related? `$A4`×$100) |
| +1C | `$00270028` | word pair |
| +24 | `$00FF60EC` | |
| +28 | `$00C01598` | self-relative pointer |
| +2C | `$10` | |

### `$c6` slot → `$00C03D46`

| Off | Value | Notes |
|-----|-------|-------|
| +00 | 0 | |
| +04 | `$00C05D10` | pointer |
| +08 | 0 | |
| +0C | `$002C0081` | |
| +10 | `$40000000` | |
| +14.. | various small ints / pointers | |
| +40 | `$00C00276` etc. | more pointers |

## Role hypothesis

Mode-dependent **draw / scene context blocks**.  The mode byte `$220`
indexes table `$151a` to compute which block is active; `$be` and `$c6`
slots then point at the chosen pair.  Consumers read through these
pointers rather than hard-coding structure addresses.

Exact field meanings need a session where the install overlay is
resident, or cross-refs from draw-mode bodies that load `($be)` /
`($c6)`.

## Status

- [x] Distinguished from `$c6` motion function
- [x] Install algorithm recovered (table `$151a`, index `$220`)
- [x] Live targets are data, not code
- [x] Structure snapshots captured from in-flight dump
- [ ] Field-level meaning of context blocks
- [ ] Capture dump with install overlay resident for full table decode
