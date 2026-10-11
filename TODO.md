# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, library/path/credits documentation
- [x] Early init disassembly (Forbid → OpenLibrary → $30)
- [x] High-level flow + interactive C++ state machine
- [x] Absolute-address calling convention documented
- [x] JOTD WHDLoad slave imported; PL_ sites mapped
- [x] flake.nix added
- [x] Protection, empty-loop, keyboard, VBL, picture-wait sites documented
- [x] Located blitter / custom-chip access region (0x288c8–0x2aa30)

## Next Steps
1. [ ] Map absolute call targets from picture-wait (0x0680) to file
      offsets (load-base assumption or emulator tracing)
2. [ ] Disassemble blitter region to identify bitplane pointers and
      polygon-fill / copper setup
3. [ ] Trace keyboard test (0x1590) into menu logic
4. [ ] Resolve library opened at 0x4b8
5. [ ] Expand C++ state machine with ILBM / placeholder rendering
6. [ ] Identify main flight loop and 3D transform entry points
7. [ ] Begin RE of 3D math

## Analysis Notes
- PL_ offsets == file offsets
- Blitter-wait loops confirm 0x28800+ is graphics drawing code
- splsh is empty on disk; title graphics live inside the binary or are procedural
- Picture-wait calls absolute $ef4 / $996 / $cce / $1c

## Bundle History
- fa18-port-011.1-… (superseded)
- (next: 012.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
