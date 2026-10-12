# Overlay residency (in-flight dump)

Snapshot taken while airborne with rockets fired.  Slow RAM dump
base `$00C00000`, 512 KB.

## Summary

| Category | Count |
|----------|------:|
| CODE hunks on disk | 77 |
| Resident mid-flight | ~72 |
| Not resident | ~5 |

Nearly the entire game code is resident during flight.  The few
missing hunks are large assets likely used only in menus / title /
debrief.

## Key resident hunks

| File | Size | Live base | Role |
|------|-----:|-----------|------|
| `0x300` | 41804 | `$00C0CA08` | **PRIMARY** – engine, transform `$77d8`, object loop |
| `0x12414` | 4420 | `$00C1C740` | Draw modes $0f–$26; **`$be`/`$c6` vector-slot install** |
| `0x1C1DC` | 1824 | `$00C231E0` | DDA / span modes $01–$0a region |
| `0x1EA04` | 2244 | `$00C25140` | **`$c6` motion function** (offset `$C6` → `$00C25206`) |
| `0x1F3D0` | 2832 | `$00C25A10` | **`$59e` vertex/list walker** (offset `$59E` → `$00C25FAE`) |
| `0x28274` | 2068 | `$00C2D7C8` | **Geometry** – mode $1b line, $16 walker, perspective |
| `0x28B4C` | 4644 | `$00C2DFE8` | **`$5ee` edge/span helper** (offset `$5EE` → `$00C2E5D6`) |
| `0x4BF14` | 1180 | `$00C4F010` | Action create/clear region |

## Not resident (in this snapshot)

| File | Size | Notes |
|------|-----:|-------|
| `0x104B8` | 20 | tiny stub |
| `0x38DF8` | 456 | |
| `0x40F58` | 11604 | large – likely title/menu graphics code |
| `0x43CB8` | 25564 | largest missing – menu / debrief / briefing |
| `0x4A0A0` | 5800 | |

## Vector-slot install is resident

Earlier analysis noted the install overlay (file `0x125D4`) was “not
found” by a strict byte search (relocated abs operands).  Hunk-level
matching shows the whole `0x12414` hunk **is** at `$00C1C740`.

Live symbols from that hunk:

| Static | Live | Value / notes |
|--------|------|---------------|
| `$151a` (select table) | `$00C1C33A` | word table, index = mode×4 |
| `$220` (mode index) | `$00C44588` | **= 3** in this snapshot |

Table `$151a` entries are scaled offsets (observed values 0, ±4096,
±8192, ±12288) used to compute the `$be` / `$c6` context-block
pointers from a base in registers d2/d4/d6.

## Implications

1. **Static `jsr $c6` / `jsr $5ee`** resolve via relocatable hunk
   offsets — both target hunks are resident and previously decoded.
2. **Vector-slot install** can be single-stepped in a live session;
   table `$151a` and mode `$220` are available.
3. **Draw-mode bodies** for $01–$0a and $0f–$26 live in resident
   hunks at known bases — ready for targeted disassembly.
4. Missing hunks are not needed for the core flight loop.

## Method

CODE hunk payloads matched against `fa18_inflight_code.bin` by
multi-anchor signature search (8-byte sequences, rejecting zeros and
abs.l placeholders).  Live base = dump_offset − anchor_offset +
`$00C00000`.
