# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, early init, high-level flow, state machine
- [x] Absolute calling convention + load model (file = 0x300 + runtime)
- [x] JOTD WHDLoad offsets mapped
- [x] flake.nix
- [x] Blitter region located
- [x] Picture-wait function ($01c8 / 0x4c8–0x6cc) fully bounded
- [x] $996 structure walker documented
- [x] $ef4 bounds-checked consume helper documented
- [x] $cce chunk-list advance documented
- [x] Caller at 0x9d84 identified (args from struct at $10)

## Next Steps
1. [ ] Identify $84 (setup/alloc before picture-wait) and who fills $10
2. [ ] Resolve jsr $30 target after startup
3. [ ] Disassemble blitter region for bitplane pointers / polygon fill
4. [ ] Trace keyboard test (0x1590) into menu logic
5. [ ] Expand C++ state machine with ILBM / placeholder rendering
6. [ ] Identify main flight loop and 3D transform entry points

## Analysis Notes
- Custom chunked resource descriptor (not raw ILBM tags at $996/$ef4/$cce)
- Error codes -3,-6,-7,-8,-9 used across the helpers
- On-disk ILBMs are standard 5-plane ByteRun1

## Bundle History
- fa18-port-017.1-… (superseded)
- (next: 018.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
