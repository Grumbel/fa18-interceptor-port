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
- [x] Blitter register setup documented (BLTCON1, BLTADAT, modulo 40)
- [x] C++ display constants + line-draw stub; Flight state demo horizon

## Next Steps
1. [ ] Identify blitter line-draw / polygon-fill entry in hunk 35/36
2. [ ] Map low-memory vars $328, $36e, $b6, $324
3. [ ] Trace keyboard test into menu logic
4. [ ] Identify main flight loop / 3D projection
5. [ ] Expand C++ 3D stub (points, simple wireframe)
6. [ ] Real state transitions from RE’d logic

## Analysis Notes
- Playfield 320×180, blitter modulo 40 bytes
- Hunk 35: geometry prep; hunk 36: blitter registers
- Horizon/crosshair demo in Flight state uses recovered coordinates

## Bundle History
- fa18-port-024.1-… (superseded)
- (next: 025.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
