# FA-18 Interceptor – Binary Structure Notes

## File
`assets/F-18_Interceptor_amiga` (311 832 bytes)

## Format
Amiga Hunk executable with **overlays**.

- Starts with standard `HUNK_HEADER` (0x000003F3)
- Header table size field = 185 (indicates large overlay table)
- Hundreds of subsequent hunks: many small `HUNK_CODE`, `HUNK_DATA`,
  `HUNK_RELOC32/16/8`, `HUNK_SYMBOL`, `HUNK_END`, and `HUNK_BREAK`
- This layout is typical of Amiga programs that load code segments on demand
  to stay within the limited Chip/Fast RAM of an A500-era machine.

## Libraries used (from strings)
- dos.library
- graphics.library
- intuition.library
- audio.device
- input.device / keyboard.device / gameport.device / timer.device

## File references (from strings)
- `df0:pix/splsh`, `df0:pix/inst5`, `df0:pix/frnt5`
- `df0:config`
- Many `df0:text/text*` files (some only present in cracked HD installs)

## Notable UI / mission strings (excerpt)
- "F/A-18 INTERCEPTOR"
- "YOU ARE NOW OFFICIALLY ENLISTED"
- Qualification / selectable missions (F1–F6)
- Alerts: IR MISSILE, RH MISSILE, CRUISE MISSILE
- Carrier landing / launch messages
- Credits: Bob Dinnerman, Dave Warhol, Moses Ma, Electronic Arts San Mateo

## Graphics assets on disk
- `pix/frnt5`, `pix/inst5` – small 5-plane ILBMs (converted to PNG)
- `pix/splsh` – empty (0 bytes) on original ADF; splash graphics likely
  reside inside the executable or are generated

## Next RE targets
1. Parse the overlay table so individual segments can be loaded/relocated
2. Locate the true entry point and initial OpenLibrary calls
3. Identify planar bitmap / copper-list data for title & cockpit
4. Map the 3D transformation / projection routines
