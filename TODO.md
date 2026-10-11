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

## Next Steps
1. [ ] Identify call sites that invoke slot-create with indices 35 / 36
2. [ ] Refine geometric meaning of d0–d3/d7 for line endpoints
3. [ ] Find runtime writers that fill matrix coeffs at $be6–$bf6
4. [ ] Trace glue from transform results to blitter overlay entry
5. [ ] Perspective divide / reciprocal location
6. [ ] Object structure layout beyond +$26
7. [ ] Populate real object list from mission data once format known

## Analysis Notes
- Picture-wait $01c8: sole caller file 0x9d84 in func 0x9cca
- Action table base 0, 4-byte entries, runtime-filled by overlay create/clear
- Structure fields: +8=$01660000, +c=$003f0000, +10=1
- $52b shared: bit1 set by picwait path, bit7 tested by $64c
- Playfield 320×180; $155 key; $36e draw mode
- Blitter: BLTCON0 $09f0 (primary), also $0dfc/$0d0c/$0d3c/$0fec/$0f4c
- Blitter scratch: $330/$334/$338/$33e written just before BLTSIZE
- Transform: file 0x7ad8; M[3][3] words at $be6–$bf6; (Σ m*v) >> 6 → $422/$426/$42a
- Input vector: X=-2(a6), Y=-4(a6), Z=-6(a6)
- Transform caller: bsr at file 0x77f6 inside object iteration loop
- Frame: file 0x1424–0x1812 ($1124); object loop 0x75dc–0x784a ($72dc)
- Object loop: counter -16(a6) += 2; object ptr $c; tables $b8f/$bd1

## Bundle History
- fa18-port-035.1-matrix-fixedpoint-dc0339a.bundle (superseded)
- (next: 036.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
