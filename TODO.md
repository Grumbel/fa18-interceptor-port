# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, library/path/credits documentation
- [x] Early init disassembly (Forbid → OpenLibrary → $30)
- [x] High-level flow sketch from strings
- [x] Documented absolute-address calling convention (~981 jsr abs.l)
- [x] Minimal C++ Game state machine
- [x] Interactive state machine (SPACE/ENTER/BACKSPACE) + colour feedback per state

## Next Steps
1. [ ] Resolve library opened at 0x4b8 and subsequent device/file opens
2. [ ] Trace control flow from early init to enlistment (string addresses as anchors)
3. [ ] Locate planar bitmaps / copper lists for title & cockpit
4. [ ] Expand state machine with on-screen text / ILBM rendering
5. [ ] Identify main flight loop and 3D transform entry points
6. [ ] Begin RE of 3D math (matrix / projection / possible sin tables)
7. [ ] Asset pipeline for the existing ILBMs (correct 5-plane palette)

## Analysis Notes
- Fixed / low absolute addressing for almost all inter-routine calls
- Large zero regions near end of file
- Few classic LVO calls; OS interaction after bases are cached
- Several multi-KB DATA hunks may contain tables or compressed graphics

## Bundle History
- fa18-port-006.1-… (superseded)
- (next: 007.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
