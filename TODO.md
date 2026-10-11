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
- [x] Action-table layout + dispatcher; create/clear live in overlay hunk @ file 0x4bf0c
- [x] Blitter line/fill entry points identified (BLTCON0 $09f0 / $0dfc family around file 0x29a88)
- [x] Blitter register/arg mapping for primary line entry; GfxState + draw stubs expanded
- [x] Simple wireframe runway demo in Flight state (clamp + line path)

## Next Steps
1. [ ] Identify call sites that invoke slot-create with indices 35 / 36
      (still open – overlay-relative or vector-registered)
2. [ ] Refine geometric meaning of d0–d3/d7 for line endpoints
3. [ ] Main flight loop / 3D projection entry
4. [ ] Drive C++ state transitions from RE’d keyboard/action logic
5. [ ] Replace demo geometry with real projected vertices once projector found

## Analysis Notes
- Picture-wait $01c8: sole caller file 0x9d84 in func 0x9cca
- Action table base 0, 4-byte entries, runtime-filled by overlay create/clear
- Structure fields: +8=$01660000, +c=$003f0000, +10=1
- $52b shared: bit1 set by picwait path, bit7 tested by $64c
- Playfield 320×180; $155 key; $36e draw mode
- Blitter: BLTCON0 $09f0 (primary), also $0dfc/$0d0c/$0d3c/$0fec/$0f4c
- Blitter scratch: $330/$334/$338/$33e written just before BLTSIZE

## Bundle History
- fa18-port-031.1-blitter-line-entries-dc0339a.bundle (superseded)
- (next: 032.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
