# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, library/path/credits documentation
- [x] Early init disassembly (Forbid → OpenLibrary → $30)
- [x] High-level flow sketch + interactive C++ state machine
- [x] Absolute-address calling convention documented
- [x] JOTD WHDLoad slave imported
- [x] flake.nix added
- [x] Protection offset 0x1ce70 confirmed
- [x] Mapped and disassembled key PL_ sites (empty-loop, keyboard,
      speed/VBL, picture-wait, access-faults)

## Next Steps
1. [ ] Follow the picture-wait / title path (0x06b4) to locate splash
      graphics loading
2. [ ] Trace keyboard test (0x1590) into the main input & menu logic
3. [ ] Resolve the library opened at 0x4b8 and later device/file opens
4. [ ] Locate planar bitmaps / copper lists for title & cockpit
5. [ ] Expand C++ state machine with on-screen text / ILBM rendering
6. [ ] Identify main flight loop and 3D transform entry points
7. [ ] Begin RE of 3D math

## Analysis Notes
- PL_ offsets == file offsets for this binary
- Empty-loop at 0x08e2 is a classic timing busy-wait (absolute jsrs)
- Keyboard state read from low absolute addresses ($155, $206, …)
- Protection writes #$ff to $27c; slave forces #$01 and sets flags

## Bundle History
- fa18-port-010.1-… (superseded)
- (next: 011.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
