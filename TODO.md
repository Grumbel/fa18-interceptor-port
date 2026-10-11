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
- [x] Added flake.nix (Nix build + devShell with SDL2/cmake)

## Next Steps
1. [ ] Map the PL_ offsets from the WHDLoad slave onto the file offsets
      of our binary (or determine the LoadSeg base)
2. [ ] Resolve the library opened at file 0x4b8 and later device/file opens
3. [ ] Trace control flow from early init to enlistment
4. [ ] Locate planar bitmaps / copper lists for title & cockpit
5. [ ] Expand state machine with on-screen text / ILBM rendering
6. [ ] Identify main flight loop and 3D transform entry points
7. [ ] Begin RE of 3D math

## Analysis Notes
- JOTD slave marks protection flags at runtime $19737 / $3a964
- Speed regulation / VBL hooks at $08286 / $092e6
- Protection patch comment: offset 1ce70 (13fc00ff → 13fc0001)
- Fixed / low absolute addressing for almost all inter-routine calls

## Bundle History
- fa18-port-009.1-… (superseded)
- (next: 010.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
