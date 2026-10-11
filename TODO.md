# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, early init, high-level flow, state machine
- [x] Load model + low-memory vector table distinction
- [x] JOTD WHDLoad offsets; flake.nix; blitter region located
- [x] Picture-wait path ($01c8, $ef4, $996, $cce) documented
- [x] Minimal ILBM loader (ByteRun1) + display frnt5/inst5 in SDL window

## Next Steps
1. [ ] Find trampoline installer for low-memory vectors ($00–$100)
2. [ ] Find who writes structure pointer into $10
3. [ ] Disassemble blitter region for bitplane pointers / polygon fill
4. [ ] Trace keyboard test (0x1590) into menu logic
5. [ ] Identify main flight loop and 3D transform entry points
6. [ ] Begin RE of 3D math
7. [ ] Improve rendering (scale assets, add text, real state transitions)

## Analysis Notes
- $30 used as library base via move.l $30,a6 / jsr LVO(a6)
- dos.Open (-30) at file 0x414; result stored at $48
- On-disk ILBMs renderable; title graphics still inside executable

## Bundle History
- fa18-port-019.1-… (superseded)
- (next: 020.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
