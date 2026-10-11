# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, early init, high-level flow, state machine
- [x] Load model for primary CODE (hunk 0) + vector-table distinction
- [x] JOTD WHDLoad offsets; flake.nix; ILBM loader
- [x] Picture-wait path documented
- [x] HUNK_HEADER: 185-slot table, 137 hunks
- [x] Hunks 71/72 are zero-filled (runtime buffers); blitter in hunks 35–39
- [x] Address table at 0xc39c (primary-segment offsets)
- [x] ILBM assets drawn 2× nearest-neighbour

## Next Steps
1. [ ] Determine whether all hunks are resident (LoadSeg) or true overlays
2. [ ] Analyse blitter hunks 35–36 relative to payload start
3. [ ] Trace address table at 0xc39c (dispatcher / entry points)
4. [ ] Trace keyboard test into menu logic
5. [ ] Identify main flight loop / 3D entry
6. [ ] Expand C++ rendering and real state transitions

## Analysis Notes
- Hunks 71/72: all-zero file regions → runtime bitmaps / buffers
- Blitter code: hunks 35–39 (file ~0x28274+)
- Possible function-offset table at DATA 0xc39c
- Minimal dos I/O in primary segment (Open/Close only at startup)

## Bundle History
- fa18-port-022.1-… (superseded)
- (next: 023.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
