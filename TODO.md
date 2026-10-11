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
- [x] 3D transform candidate at file 0x7ad8 (matrix muls → $422/$426/$42a)
- [x] C++ Mat3/Vec3 + project stub; rotating cube demo in Flight
- [x] Full flight-frame call graph: $1124 → $72dc → $77d8 → blitter
- [x] Exact 3×3 fixed-point matrix layout ($be6–$bf6) and asr #6 format
- [x] Object-list iteration structure (-16(a6) counter, $c object ptr, $b8f/$bd1 tables)
- [x] C++ state transitions driven by recovered keyboard path ($155/$206/$207)
- [x] World translation ($b68/$b6c/$b70) + object structure offsets 0/4/8
- [x] Post-transform draw path entry at file 0x784c

## Next Steps
1. [ ] Identify call sites that invoke slot-create with indices 35 / 36
2. [ ] Refine geometric meaning of d0–d3/d7 for line endpoints
3. [ ] Find runtime writers that fill matrix coeffs at $be6–$bf6
4. [ ] Resolve low vectors $c6 / $68a used by post-transform draw
5. [ ] Perspective divide / reciprocal location
6. [ ] Fuller object structure layout
7. [ ] Populate real object list from mission data once format known

## Analysis Notes
- Transform results + view translation → object+0/+4/+8 (world X/Y/Z)
- Post-transform draw at file 0x784c calls vectors $c6 and $68a
- View translation: $b68 (X), $b6c (Y), $b70 (Z)
- Object+$26 = flags; +$28 / +$30 manipulated by draw path
- Frame $1124 → object loop $72dc → transform $77d8 → draw 0x784c

## Bundle History
- fa18-port-036.1-object-list-input-state-dc0339a.bundle (superseded)
- (next: 037.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
