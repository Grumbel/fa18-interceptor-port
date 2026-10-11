# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, early init, high-level flow, state machine
- [x] Load model for primary CODE + vector-table distinction
- [x] JOTD WHDLoad offsets; flake.nix; ILBM loader
- [x] Picture-wait path documented
- [x] HUNK_HEADER parsed; runtime zero-buffers identified
- [x] Blitter hunk 35: viewport 320×180, clamp, mode stubs
- [x] Blitter register setup (BLTCON1, BLTADAT, modulo 40)
- [x] C++ display constants + line-draw stub; Flight demo horizon
- [x] Low-mem vars $36e/$324/$b6/$328/$112 mapped

## Next Steps
1. [ ] Identify blitter line-draw / polygon-fill entry in hunk 35/36
2. [ ] Trace keyboard test into menu logic
3. [ ] Identify main flight loop / 3D projection
4. [ ] Expand C++ 3D stub (points, simple wireframe)
5. [ ] Real state transitions from RE’d logic

## Analysis Notes
- $36e = draw mode, $324 = pen, $b6 = plane mask, $112 = phase
- Playfield 320×180, blitter modulo 40 bytes
- Hunk 35 geometry prep; hunk 36 blitter registers

## Bundle History
- fa18-port-025.1-… (superseded)
- (next: 026.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
