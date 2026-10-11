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
- [x] Picture-wait function body outlined; private helpers $996 / $cce identified

## Next Steps
1. [ ] Fully reverse $996 (large stack frame – possible image/header parser)
      and $cce; identify any file or memory reads for title graphics
2. [ ] Disassemble blitter region for bitplane pointers / polygon fill
3. [ ] Trace keyboard test (0x1590) into menu logic
4. [ ] Resolve what jsr $30 actually targets after init
5. [ ] Expand C++ state machine with ILBM / placeholder rendering
6. [ ] Identify main flight loop and 3D transform entry points
7. [ ] Begin RE of 3D math

## Analysis Notes
- file_offset = 0x300 + runtime_addr for main CODE segment
- Picture-wait function ends at 0x6cc; next function sets $112 = 3
- $996 and $cce are private to the picture-wait path
- $ef4 is a shared helper

## Bundle History
- fa18-port-014.1-… (superseded)
- (next: 015.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
