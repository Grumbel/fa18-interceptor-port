# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, early init, high-level flow, state machine
- [x] Load model for primary CODE segment + vector-table distinction
- [x] JOTD WHDLoad offsets; flake.nix
- [x] Picture-wait path documented ($01c8, $ef4, $996, $cce)
- [x] ILBM loader + display of frnt5/inst5
- [x] Multi-hunk overlay architecture identified; blitter code is in overlays

## Next Steps
1. [ ] Parse HUNK_OVERLAY table to recover overlay load addresses
2. [ ] Analyse blitter overlay hunks relative to their own bases
3. [ ] Find trampoline installer for low-memory vectors
4. [ ] Trace keyboard test into menu logic
5. [ ] Identify main flight loop / 3D entry (likely in an overlay)
6. [ ] Expand C++ rendering (scale assets, text, real transitions)

## Analysis Notes
- Primary CODE: file 0x300, 41804 bytes, runtime 0
- Overlays: dozens of CODE hunks; blitter at file ~0x2826c+
- Overlay jsr targets need overlay table or dynamic tracing
- On-disk ILBMs renderable in the SDL port

## Bundle History
- fa18-port-020.1-… (superseded)
- (next: 021.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
