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
- [x] Blitter hunk 35: viewport 320×180; register setup; low-mem vars
- [x] C++ display/draw/gfx_state stubs; Flight demo horizon
- [x] Keyboard path at 0x1590 documented ($155 key, $206 enable, menu cmp)

## Next Steps
1. [ ] Fully reverse keyboard dispatch (mode 1/2 handlers $5b6/$64c/$878)
2. [ ] Identify blitter line-draw / polygon-fill entry in hunk 35/36
3. [ ] Identify main flight loop / 3D projection
4. [ ] Expand C++ 3D stub (points, simple wireframe)
5. [ ] Real state transitions from RE’d logic

## Analysis Notes
- $155 = key code (12 read sites); central handler at 0x1590
- $206 = input enable; $207 = mode; $2ac = menu index
- Draw: $36e mode, $324 pen, $b6 plane mask
- Playfield 320×180

## Bundle History
- fa18-port-026.1-… (superseded)
- (next: 027.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
