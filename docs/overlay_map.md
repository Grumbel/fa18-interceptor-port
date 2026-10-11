# Overlay / HUNK Map

The binary is heavily overlaid.  Key CODE hunks:

| File offset | Size | Role |
|-------------|------|------|
| 0x0002f8 | 0xa34c B | **Primary CODE** – main engine, transform, object loop, blitter |
| 0x01240c | 0x1144 B | Modes $0f–$26; `$c6` dynamic install |
| 0x01c1d4 | 0x720 B | DDA/span modes $01–$0a |
| 0x02826c | 0x814 B | **Geometry**: mode $1b line, $16 walker, perspective, edge helpers |
| 0x04bf0c | 0x49c B | **Action create/clear** (slots, structure init) |

Dozens of additional CODE hunks (0xd9ec–0x4a098) hold remaining
draw modes, HUD, mission logic, and data.  Absolute addresses inside
overlays are relocatable; static analysis sees file offsets only.

## Load model reminder

Primary CODE: `file_offset = runtime_address + 0x300`.

Overlay CODE: loaded at runtime to addresses determined by the
overlay manager; vector slots (`$12`, `$5ee`, `$1a6`, …) are
installed after load.  Dynamic trace is required to resolve final
targets.

## Hunk inventory (full binary scan)

| Type | Count |
|------|------:|
| CODE | 68 |
| DATA | 6 |
| BSS | 1 |
| RELOC32 | 56 |
| DEBUG | 70 |
| END | 4 |
| NAME | 2 |
| HEADER | 1 |

Largest CODE: primary at file **0x300**, 41804 bytes.  
Geometry CODE: file **0x28274**, 2068 bytes.

## Vector install conclusion (static)

Exhaustive scan of all 68 CODE hunks:

- **No** `MOVE.L …, $12` or `MOVE.L …, $5ee` (abs.l or abs.w)
- **Yes** `MOVE.L D6, $be` / `$c6` at file 0x125f6 / 0x12664 (table `$151a`)
- LEA(pc)+MOVE.L patterns install trap handlers to **$4** (Exec), not draw vectors

Therefore `$12` and `$5ee` are filled only after relocation by the
overlay manager writing through a base+offset that does not appear as
a fixed absolute address in the on-disk image.  Live memory watch on
those slots during overlay load is the only practical way to catch the
installer and the final targets.
