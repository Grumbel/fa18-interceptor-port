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
