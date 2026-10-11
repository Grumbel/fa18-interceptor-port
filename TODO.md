# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, library/path/credits documentation
- [x] Early init disassembly (Forbid → OpenLibrary → $30)
- [x] High-level flow sketch from strings
- [x] Documented absolute-address calling convention
- [x] Minimal interactive C++ Game state machine
- [x] Noted fixed-load-base limitation for string xrefs
- [x] Imported JOTD WHDLoad slave source and extracted key patch offsets
      (protection flags, empty-loop, keyboard, VBL/speed, picture wait)

## Next Steps
1. [ ] Map the PL_ offsets from the WHDLoad slave onto the file offsets
      of our binary (or determine the LoadSeg base) so we have concrete
      runtime addresses for protection, keyboard and main loop hooks
2. [ ] Resolve the library opened at file 0x4b8 and later device/file opens
3. [ ] Trace control flow from early init to enlistment
4. [ ] Locate planar bitmaps / copper lists for title & cockpit
5. [ ] Expand state machine with on-screen text / ILBM rendering
6. [ ] Identify main flight loop and 3D transform entry points
7. [ ] Begin RE of 3D math

## Analysis Notes
- JOTD slave marks “section 20” as protection and sets flags at
  runtime offsets $19737 and $3a964
- Speed regulation / VBL hooks at $08286 / $092e6
- Keyboard test at $01590
- Fixed / low absolute addressing for almost all inter-routine calls
- String xrefs require knowing the load base

## Bundle History
- fa18-port-008.1-… (superseded)
- (next: 009.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
