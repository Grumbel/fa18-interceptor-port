# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, early init, high-level flow, state machine
- [x] Load model: file = 0x300 + runtime (for addresses ≳ $200)
- [x] Low-memory vector table distinction documented (< ~$100)
- [x] JOTD WHDLoad offsets mapped; flake.nix
- [x] Blitter region located
- [x] Picture-wait path: $01c8, $ef4, $996, $cce documented
- [x] $84 identified as runtime vector (16+ call sites); $10 setup partial

## Next Steps
1. [ ] Find the code that installs trampolines into the low-memory
      vector table (search for moves of JMP opcodes / code addresses
      into $00–$100)
2. [ ] Find who writes the structure pointer into $10
3. [ ] Disassemble blitter region for bitplane pointers / polygon fill
4. [ ] Trace keyboard test (0x1590) into menu logic
5. [ ] Expand C++ state machine with ILBM / placeholder rendering
6. [ ] Identify main flight loop and 3D transform entry points

## Analysis Notes
- Static resolution reliable for runtime ≳ $200
- Low addresses ($00–$100) are runtime-patched vectors
- Custom chunked resource descriptor on picture-wait path
- On-disk ILBMs: 5-plane ByteRun1

## Bundle History
- fa18-port-018.1-… (superseded)
- (next: 019.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
