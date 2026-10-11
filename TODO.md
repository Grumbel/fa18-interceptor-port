# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, library/path/credits documentation
- [x] Early init disassembly (Forbid → OpenLibrary → $30)
- [x] High-level flow sketch from strings
- [x] Documented absolute-address calling convention
- [x] Minimal C++ Game state machine
- [x] Interactive state machine + colour feedback + dynamic window title
- [x] Noted that file-offset string addresses cannot be used directly as
      absolute immediates (fixed load base)

## Next Steps
1. [ ] Determine the runtime load address (or treat the whole binary as a
      flat image loaded at a chosen base for analysis)
2. [ ] Resolve the library opened at 0x4b8 and later device/file opens
3. [ ] Trace control flow from early init to enlistment
4. [ ] Locate planar bitmaps / copper lists for title & cockpit
5. [ ] Expand state machine with on-screen text / ILBM rendering
      (SDL2_image or minimal PNG loader)
6. [ ] Identify main flight loop and 3D transform entry points
7. [ ] Begin RE of 3D math

## Analysis Notes
- Fixed / low absolute addressing for almost all inter-routine calls
- String xrefs require knowing the load base
- Large zero regions near end of file
- Few classic LVO calls; OS interaction after bases are cached

## Bundle History
- fa18-port-007.1-… (superseded)
- (next: 008.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
