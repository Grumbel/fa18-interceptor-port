# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, early init, high-level flow, state machine
- [x] Load model for primary CODE (hunk 0) + vector-table distinction
- [x] JOTD WHDLoad offsets; flake.nix; ILBM loader
- [x] Picture-wait path documented
- [x] HUNK_HEADER parsed: 185-slot table, 137 non-zero hunks, ~299 KB
- [x] No standard HUNK_OVERLAY – custom overlay loader

## Next Steps
1. [ ] Locate custom overlay loader / runtime address table in hunk 0
2. [ ] Analyse large hunks 71/72 (possible embedded graphics)
3. [ ] Analyse blitter overlay hunks relative to own bases
4. [ ] Trace keyboard test into menu logic
5. [ ] Identify main flight loop / 3D entry
6. [ ] Expand C++ rendering

## Analysis Notes
- Hunk 0: 41804 bytes @ file 0x300, runtime 0
- 137 resident/overlay hunks; hunk 62 is chip mem
- Largest: hunk 72 (25564 B), hunk 71 (11604 B)
- Custom overlay scheme (no 0x3F5 marker)

## Bundle History
- fa18-port-021.1-… (superseded)
- (next: 022.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
