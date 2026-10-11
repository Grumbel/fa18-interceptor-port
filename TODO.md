# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, early init, high-level flow, state machine
- [x] Load model for primary CODE + vector-table distinction
- [x] JOTD WHDLoad offsets; flake.nix; ILBM loader
- [x] Picture-wait path; HUNK_HEADER; blitter 320×180; low-mem vars
- [x] Keyboard path 0x1590; handlers $5b6/$64c/$878
- [x] $64c chain corrected: $9d7a → table dispatcher 0x9f7c (actions 35/36)

## Next Steps
1. [ ] Identify the action table at absolute 0 (entries 35/36)
2. [ ] Find all callers of picture-wait $01c8
3. [ ] Blitter line-draw / polygon-fill entry in hunk 35/36
4. [ ] Main flight loop / 3D projection
5. [ ] Expand C++ 3D stub; real state transitions

## Analysis Notes
- $64c → $9d7a → 0x9f7c(35/36) table dispatch; flags $52a/$52b
- Picture-wait $01c8 still entered from file 0x9d84 ($84 setup path)
- Playfield 320×180; $36e draw mode; $155 key

## Bundle History
- fa18-port-028.1-… (superseded)
- (next: 029.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
