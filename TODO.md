# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, early init, high-level flow, state machine
- [x] Load model for primary CODE + vector-table distinction
- [x] JOTD WHDLoad offsets; flake.nix; ILBM loader
- [x] Picture-wait path documented
- [x] HUNK_HEADER parsed; runtime zero-buffers; blitter 320×180
- [x] Low-mem graphics vars; C++ draw/gfx/input stubs
- [x] Keyboard path at 0x1590; mode handlers $5b6/$64c/$878 outlined

## Next Steps
1. [ ] Deepen $64c path into picture-wait / title transition
2. [ ] Identify blitter line-draw / polygon-fill entry in hunk 35/36
3. [ ] Identify main flight loop / 3D projection
4. [ ] Expand C++ 3D stub (points, simple wireframe)
5. [ ] Real state transitions from RE’d logic

## Analysis Notes
- $5b6 = frame/service tick; $64c = title proceed; $878 = resource setup
- $155 key, $206 enable, $207 mode, $2ac menu index
- Playfield 320×180; $36e draw mode; $324 pen; $b6 plane mask

## Bundle History
- fa18-port-027.1-… (superseded)
- (next: 028.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
