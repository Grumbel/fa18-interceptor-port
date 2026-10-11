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
- [x] Protection / empty-loop / keyboard / VBL / picture-wait sites documented
- [x] Blitter / custom-chip region located (0x288c8–0x2aa30)
- [x] Load model: first CODE hunk at runtime 0; file = 0x300 + runtime
- [x] Resolved picture-wait callees ($ef4, $996, $cce) to real functions

## Next Steps
1. [ ] Fully disassemble the picture-wait call chain ($ef4 / $996 / $cce)
      and identify title / splash loading
2. [ ] Disassemble blitter region for bitplane pointers / polygon fill
3. [ ] Trace keyboard test (0x1590) into menu logic
4. [ ] Resolve library opened at 0x4b8
5. [ ] Expand C++ state machine with ILBM / placeholder rendering
6. [ ] Identify main flight loop and 3D transform entry points
7. [ ] Begin RE of 3D math

## Analysis Notes
- file_offset = 0x300 + runtime_addr for the main CODE segment
- Picture-wait → $ef4 (small), $996 (large stack frame), $cce (short)
- Blitter-wait loops at 0x28800+ = graphics drawing code
- PL_ offsets == file offsets for in-segment patches

## Bundle History
- fa18-port-013.1-… (superseded)
- (next: 014.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
