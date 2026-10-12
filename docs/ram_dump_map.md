# RAM Dump Map (session dumps)

Four memory snapshots accompany this work line:

| File | Size | Contents |
|------|------|----------|
| `dumps/fa18_low.bin` | 64 KiB | `$000000`–`$00FFFF` (vectors, ExecBase, low variables) |
| `dumps/fa18_code.bin` | 512 KiB | Relocated CODE (primary + resident overlays) |
| `dumps/fa18_data.bin` | 128 KiB | Runtime DATA / tables / BSS after load |
| `dumps/fa18_stack.bin` | 512 B | Stack snapshot (live addresses in `$00Cxxxxx`) |

These dumps are the only practical way to resolve overlay vectors and
relocated absolute addresses. Static analysis of the ADF alone cannot
see the final targets of `$12`, `$5ee`, `$c6`, etc.

## Primary CODE mapping (this dump)

The first large `HUNK_CODE` (file offset **0x300**, payload 41804 bytes)
is present in `fa18_code.bin` starting at dump offset **0xa08**.

Relocation base observed for internal absolute references:

```
live_addr   = 0x00C06A70 + runtime_r
dump_offset = 0x0A08     + runtime_r
file_offset = 0x300      + runtime_r
```

Equivalently:

```
live_addr   = 0x00C06A70 + (file_offset - 0x300)
dump_offset = 0x0A08     + (file_offset - 0x300)
```

### Evidence

- 386 exact 16-byte sequence matches between primary payload and the
  dump region at `0xa08 + (file-0x300)`.
- 37 absolute longwords in the first 1 KiB of primary were fixed up:
  - majority delta `+0x00C06A70` (self-references inside the CODE segment)
  - secondary delta `+0x00C0CA08` (references that previous sessions
    already recorded as the “CODE load delta”)
- First instruction example:
  - on-disk: `23c0 0000 0040`  (`move.l d0,$40.l`)
  - in dump:  `23c0 00c0 6ab0`  (`move.l d0,$00C06AB0.l`)

The first 0xa08 bytes of `fa18_code.bin` contain other resident CODE
(prologues / small hunks) before the primary image.

## Overlay CODE present in the dump

Partial sequence matches (relocated, so only non-absolute runs match):

| File offset | Role (from overlay_map.md) | Approx dump location |
|-------------|----------------------------|----------------------|
| 0x00e4bc    | (large CODE)               | ~0x0d770             |
| 0x0104f0    |                            | ~0x0ee20             |
| 0x016aec    |                            | ~0x1399a             |
| 0x019a54    |                            | ~0x157d8             |
| 0x01c1f0    | DDA/span (partial)         | ~0x171f4             |
| 0x0282a0    | geometry walker (partial)  | ~0x217f4             |
| 0x04bf14    | action create (partial)    | ~0x43010             |

Not every overlay is resident; the snapshot was taken while many draw
vectors were still zero (see below).

## Low-memory state in this snapshot

| Address | Name / role              | Value in dump | Notes |
|---------|--------------------------|---------------|-------|
| `$0004` | ExecBase                 | `$00C00276`   | consistent with high-memory layout |
| `$0012` | draw vector              | `$081C00FC`   | **not** a CODE pointer; still unset / trap |
| `$00BE` | sibling of `$c6`         | `$08540000`   | |
| `$00C6` | post-transform / draw    | `$00000000`   | zero – overlays not yet installed |
| `$05EE` | geometry hand-off        | `$00000000`   | zero – matches “no objects active” |
| `$036E` | DrawMode                 | `$00000000`   | |
| `$04B0` | input axes (8-bit)       | `$00000000`   | |
| `$052B` | shared keyboard/picwait  | `$00000000`   | |
| `$0642` | object pool base         | `$00000000`   | |
| `$0B8F` | active object count      | `$00`         | |
| `$0BE6` | global matrix            | `$00000000`   | |

Conclusion: the dump was captured early (title / pre-flight or before
any 3-D objects were created).  That is why `$12` / `$5ee` / `$c6` are
still zero and why many overlay CODE hunks are absent.

## Stack snapshot

`fa18_stack.bin` contains live addresses that match the relocation
base derived above (`$00C06A38`, `$00C06A70` family) as well as the
higher addresses previously documented from other sessions
(`$00C2D07A`, `$00C2CFE0`, …).  The capture format appears to interleave
or word-swap some values; treat individual longs with caution until
the exact save-state layout is known.

## How to use the dumps

1. **Primary CODE** – convert any file offset `f ≥ 0x300` that belongs
   to the first CODE hunk with the formulas above.  Disassemble the
   corresponding range in `fa18_code.bin`; the absolute addresses you
   see are the true live ones for this session.
2. **Vectors** – watch `$12`, `$5ee`, `$c6`, `$be` in `fa18_low.bin`.
   In a later dump taken after objects are active they will hold the
   real overlay entry points.
3. **Cross-check** – any absolute longword that on-disk was
   `0000xxxx` and in the dump is `00C06Axx` or `00C0CAxx` confirms the
   two bases already recovered.
4. **Future dumps** – re-run `tools/map_ram_dumps.py`; the script
   recomputes the primary base from relocated abs pairs so different
   sessions (different load addresses) are handled automatically.

## Relation to previous session notes

Earlier analysis recorded:

```
ADF load deltas: CODE +$00C0CA08, data +$00C44368
live builders @ 00C2D07A / 00C2CFE0
```

Those addresses appear as **data** (pointers) inside the present
`fa18_code.bin` and `fa18_data.bin`, confirming the dumps belong to the
same or a very similar runtime configuration.  The primary CODE itself
loaded at `$00C06A70` in the snapshot we have; the `$00C0CA08` figure
is the secondary relocation base used for certain absolute references.

## Next RE steps enabled by the map

- Disassemble the live primary CODE (transform, object loop, matrix
  builders) directly from `fa18_code.bin` using the formulas above.
- Obtain a second dump taken while objects are active so that `$12`,
  `$5ee` and `$c6` become non-zero and can be followed.
- Map the remaining resident overlay hunks once a fuller snapshot is
  available.
