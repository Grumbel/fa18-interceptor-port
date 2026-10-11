# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, library/path/credits documentation
- [x] Early init disassembly (Forbid → OpenLibrary → $30)
- [x] High-level flow + interactive C++ state machine
- [x] Absolute-address calling convention + load model
- [x] JOTD WHDLoad slave imported; PL_ sites mapped
- [x] flake.nix added
- [x] Blitter region located (0x288c8–0x2aa30)
- [x] Picture-wait function outlined
- [x] $996 partially reversed – structure parser / validator (error -6)

## Next Steps
1. [ ] Finish $996 disassembly past 0xe2a; identify the structure layout
      (compare with ILBM BMHD / custom header)
2. [ ] Reverse $cce fully
3. [ ] Disassemble blitter region for bitplane pointers / polygon fill
4. [ ] Trace keyboard test (0x1590) into menu logic
5. [ ] Expand C++ state machine with ILBM / placeholder rendering
6. [ ] Identify main flight loop and 3D transform entry points
7. [ ] Begin RE of 3D math

## Analysis Notes
- file_offset = 0x300 + runtime_addr
- $996: packed bitfields, count 17, returns -6 on error, calls $ef4
- $996 / $cce are private to picture-wait path
- Blitter-wait loops at 0x28800+

## Bundle History
- fa18-port-015.1-… (superseded)
- (next: 016.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
