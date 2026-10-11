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

## Next Steps
1. [ ] Identify call sites that invoke slot-create with indices 35 / 36
      (still open – overlay-relative or vector-registered)
2. [ ] Map exact Dn argument layout of the line/fill blitter entries
3. [ ] Main flight loop / 3D projection
4. [ ] Expand C++ 3D stub; real state transitions from RE
5. [ ] Implement SDL line/fill stubs driven by the recovered clamp + BLTCON0 paths

## Analysis Notes
- Picture-wait $01c8: sole caller file 0x9d84 in func 0x9cca
- Action table base 0, 4-byte entries, runtime-filled by overlay create/clear
- Structure fields: +8=$01660000, +c=$003f0000, +10=1
- $52b shared: bit1 set by picwait path, bit7 tested by $64c
- Playfield 320×180; $155 key; $36e draw mode
- Blitter: BLTCON0 values $09f0, $0dfc, $0d0c, $0d3c, $0fec, $0f4c

## Bundle History
- fa18-port-030.1-action-table-overlay-dc0339a.bundle (superseded)
- (next: 031.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
