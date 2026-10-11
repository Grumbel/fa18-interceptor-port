# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, early init, high-level flow, state machine
- [x] Load model for primary CODE + vector-table distinction
- [x] JOTD WHDLoad offsets; flake.nix; ILBM loader
- [x] Picture-wait path; sole caller at 0x9d84 (func 0x9cca)
- [x] HUNK_HEADER; blitter 320×180; low-mem vars
- [x] Keyboard path; $64c → $9d7a → table[35/36] via 0x9f7c
- [x] Shared flag byte $52b between keyboard and picture-wait paths

## Next Steps
1. [ ] Identify what populates action table slots $8c/$90
2. [ ] Blitter line-draw / polygon-fill entry in hunk 35/36
3. [ ] Main flight loop / 3D projection
4. [ ] Expand C++ 3D stub; real state transitions from RE

## Analysis Notes
- Picture-wait $01c8: sole caller file 0x9d84 in func 0x9cca
- Action table base 0, 4-byte entries, runtime-filled
- $52b shared: bit1 set by picwait path, bit7 tested by $64c
- Playfield 320×180; $155 key; $36e draw mode

## Bundle History
- fa18-port-029.1-… (superseded)
- (next: 030.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
