# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, library/path/credits documentation
- [x] Early init disassembly (Forbid → OpenLibrary → $30)
- [x] High-level flow sketch from strings
- [x] Documented absolute-address calling convention (~981 jsr abs.l to low memory)
- [x] Minimal C++ Game state machine (Startup → Title → Enlistment → Qualification → MissionSelect → Flight → Log)

## Next Steps
1. [ ] Resolve the library opened at 0x4b8 and subsequent device/file opens
2. [ ] Trace control flow from early init to enlistment screen (using string addresses as anchors)
3. [ ] Locate planar bitmaps / copper lists for title & cockpit
4. [ ] Identify main flight loop and 3D transform entry points
5. [ ] Expand the C++ state machine with real input handling and placeholder rendering
6. [ ] Asset pipeline for the existing ILBMs (correct palette, scaling)
7. [ ] Begin RE of the 3D math (look for matrix / projection constants)

## Analysis Notes
- Game uses fixed / low absolute addresses for almost all inter-routine calls
- Large zero regions near end of file (possible BSS or padding for unpacked data)
- Very few classic jsr d16(a6) LVO calls – most OS interaction is after bases are cached

## Bundle History
- fa18-port-005.1-… (superseded)
- (next: 006.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
