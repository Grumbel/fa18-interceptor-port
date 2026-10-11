# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, early init, high-level flow, state machine
- [x] Load model for primary CODE (hunk 0) + vector-table distinction
- [x] JOTD WHDLoad offsets; flake.nix; ILBM loader (2× scale)
- [x] Picture-wait path documented
- [x] HUNK_HEADER: 185-slot table, 137 hunks; zero-filled runtime buffers
- [x] Blitter hunk 35: viewport clamp (320×180), mode stubs, ~14 primitives

## Next Steps
1. [ ] Continue hunk 35/36 – identify line-draw / fill entry points
2. [ ] Map low-memory variables $328, $36e used by blitter stubs
3. [ ] Trace keyboard test into menu logic
4. [ ] Identify main flight loop / 3D projection entry
5. [ ] Add C++ screen-space line/clamp stubs matching hunk 35
6. [ ] Expand rendering and real state transitions

## Analysis Notes
- Hunk 35: geometry prep + clamp; hunk 36: blitter register ops
- DATA 0xc39c is global pointer data, not a function dispatch table
- Viewport appears to be 320×180 (centred at 160, 90)

## Bundle History
- fa18-port-023.1-… (superseded)
- (next: 024.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
