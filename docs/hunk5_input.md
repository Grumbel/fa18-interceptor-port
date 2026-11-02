# Hunk 5: keyboard and flight-control dispatcher

Evidence: independent, bounded 68000 reconstruction in
[`retro-foundry/fa18interceptor`](https://github.com/retro-foundry/fa18interceptor/tree/a8fc677966ef224e449a76637a51792169849573/source_amiga/observed),
particularly `dispatch_direct_command_keys.asm`,
`dispatch_function_and_indexed_keys.asm`, `set_rudder_input_mask.asm`,
`apply_function_key_throttle_level.asm`, and
`update_three_axis_control_bytes.asm`. Addresses below are translated with
`tools/reference_addr.py` into **our** `$20000` LoadSeg layout. This is a
static interpretation of that project's assembled source; validate all bytes
against our uncracked executable before adding native C++ implementations.

## Raw keys: use physical Amiga codes, not labels in the other source

Several `RAW_KEY_*` symbols in its reconstructed assembly are misleading:
`RAW_KEY_M=$4C` is the *Up cursor*, `RAW_KEY_BACKSPACE=$38` is *comma*, and
`RAW_KEY_TAB=$39` is *period*. The numerical comparisons appear reliable,
but names are not a physical keyboard map. Our `amiga_rawkey()` in
`src/main.cpp` maps SDL scancodes to the Amiga raw codes, and agrees with
the following entry routes:

| Amiga key | Raw | Hunk 5 destination (our address) | Observed/structural role |
| --- | --- | --- | --- |
| Up, Down, Right, Left | `$4C-$4F` | `$2E258`/`$2E250`/`$2E2A4`/`$2E29C` | Stick directions; check route conditions |
| Comma | `$38` | `$2E2EA` | Rudder left: `$80` |
| Period | `$39` | `$2E2F0` | Rudder right: `$40` |
| Minus | `$0B` | `$2E318` | Throttle decrement route |
| Equals | `$0C` | `$2E314` | Throttle increment route |
| F1–F10 | `$50-$59` | `$2EA60` | Direct throttle levels, context-gated |
| A | `$20` | `$2E372` | Arrestor hook |
| C | `$33` | `$2EECE` | Chaff command |
| E | `$12` | `$2DE82` | Eject path (context-gated) |
| F | `$23` | `$2EE3C` | Flare command |
| G | `$24` | `$2E96E` | Landing gear |
| H | `$25` | `$2DFC0` | HUD mode |
| J | `$26` | `$2EF4E` | ECM mode |
| M | `$37` | `$2ECE8` | Map |
| R | `$13` | `$2DF24` | Radar range |
| T | `$14` | `$2DF00` | Next target (context-gated) |

Note: the table records *routes*; the input-state/context gates before them
can redirect the same raw byte. All destination addresses above were checked
against the reference's resolved Hunk map, but the higher-level semantic
names still depend on the available behavioral traces.

**Important front-end consequence:** plain F9/F10 were previously stolen for
debug menu / mesh viewer, even though both are needed by the game's throttle
(F10 repeated at full throttle selects afterburner). Host commands now use
**Ctrl+F9 / Ctrl+F10**, with Insert and the browser Menu/Models buttons kept
as other ways to reach those tools.

## Priority A: three signed axes, `$2E16C` (`$C1B410`)

No nested calls; the reconstructed source returns with `RTS`. Input is the
record base in `A1` and the two-bit direction fields at `record+0x65`.

| Bits in `record+0x65` | Record output | Step | Signed bounds |
| --- | --- | --- | --- |
| `$30`; increment code `$10` | `+0x28` | 1 | −20…20 |
| `$C0`; increment code `$40` | `+0x29` | 1 | −20…20 |
| `$0C`; increment code `$04` | `+0x2A` | 3 | −60…60 |

For an active axis, matching the increment code increments; the other
nonzero direction pattern decrements. Both operations snap across zero to
`+step` / `-step` before continuing. Results are clamped. All arithmetic is
8-bit; flags and *partial D-register writes* must match the 68000 semantics.

**Subtlety for a native implementation:** the zero-field path for the first
axis branches straight to `move.b d4,$28(a1)` without first loading `d4`.
It therefore writes the incoming `D4.b`, not necessarily an unchanged axis!
The common path then clears `D4.b`, so zero-field paths for axes two and
three write zero. Do not implement the zero-field behavior as `continue`
or assume `D4` is zero without proving its caller contract.

This routine is a good first translation *after* confirming our executable
matches this slice and obtaining native equivalence coverage for the entry.

## Priority B: rudder mask, `$2E2EA/$2E2F0/$2E2F6`

The three entries set `D2.b` to `$80`, `$40`, or zero. They then perform:

```text
new $5B4E1 = (old $5B4E1 & $3F) | D2.b
$5AB27     = D2.b
```

All three branch to the common input tail `$2EF98`, **not** to an immediate
`RTS`. The pressed entries use `move.b` and preserve the high 24 bits of `D2`;
the clear entry uses `moveq #0,d2` and clears all 32 bits. The tail may alter
more registers and queue a command. Any replacement must reproduce the
complete tail behavior and verify leftover registers, not just the two stores.

The reference traces confirm raw comma `$38` and period `$39` reach the
respective entries. Our front end already sends these raw codes from the
right gamepad stick, so this is primarily a native-code opportunity.

## Priority C: function-key throttle, `$2EA60`

`D0.b=$50..$59` becomes level 1..10 in `D4.w`. Normally the level is
multiplied by 12, with `F10` stored as `$79` rather than `$78`, at `$5AB68`.
If the control-active flag `$5AAD0` is set, the result is shifted left by
three, capped at `$03C0`, and written to both `$5AA70` and `$5AA74`.

The level-one branch has a separate `$FF` marker behavior based on `$5B4A7`
and previous `$5AB68`; it is not covered by the reference's F10 trace.
Like the rudder entries, this routine branches to the shared tail `$2EF98`.
The native translation must preserve that path and full register/CCR effects.

## Test plan before registering natives

1. With the original ADF available, use `tools/adf.py extract-one` and
   `tools/disasm.py` to compare the three source spans byte-by-byte against
   our patched image. The other project used a different disk variant.
2. Add targeted scripted flight events for comma/period down/up and
   F1/F9/F10 (including the second F10 for afterburner), plus cases for
   the `A1+0x65` zero-field / clamp boundaries.
3. Register **one** small `NATIVE_ASM` entry at a time, with exactly the
   original register-write semantics; do not bypass `$2EF98` by accident.
4. Run `tests/check_native.sh` with the user-supplied ADF, including
   `FA18_VERIFY_ALL=1` where necessary. No native implementation should be
   committed until this gate passes.

This work is independently useful for reading input traces, even before any
new C++ translations are enabled.
