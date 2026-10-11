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
- [x] $996 fully bounded (0xc96–0xef6) and control flow documented;
      structure parser with error codes -6/-7; not raw ILBM tag parser

## Next Steps
1. [ ] Identify who builds the structure passed to $996
2. [ ] Reverse $ef4 in the context of these calls
3. [ ] Resolve vector at $0000 after startup
4. [ ] Reverse $cce
5. [ ] Disassemble blitter region for bitplane pointers / polygon fill
6. [ ] Trace keyboard test (0x1590) into menu logic
7. [ ] Expand C++ state machine with ILBM / placeholder rendering
8. [ ] Identify main flight loop and 3D transform entry points

## Analysis Notes
- file_offset = 0x300 + runtime_addr
- $996: 0xc96–0xef6, packed structure walker, errors -6/-7
- On-disk ILBMs are standard 5-plane ByteRun1; $996 does not parse FORM/BMHD tags directly
- Blitter-wait loops at 0x28800+

## Bundle History
- fa18-port-016.1-… (superseded)
- (next: 017.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
