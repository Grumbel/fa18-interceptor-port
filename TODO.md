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
- [x] Draw-mode dispatcher: move.w #MODE,$36e / jsr $12 (45 modes; $1b = line)
- [x] $c6/$68a calling convention (index arg, status return); multi-object Flight demo
- [x] Blitter Dn geometry: $352=Y, ×40 row-bytes, BLTCPT/SIZE/MOD mapping
- [x] Line endpoints $350/$352 (Y0/Y1); BLTSIZE = (dy+1)<<6 | width; $b2 bitplane base
- [x] X path: lsr#4 word offset, modulo = 39−2·Δx; param block $34c–$352; vertex buf $3000
- [x] Action create call sites for slots 35/36 (file 0x9960, called from early init 0x756)
- [x] Param block confirmed X0/X1/Y0/Y1; clipper before movem; clear wrappers; perspective in overlays

## Next Steps
1. [ ] Find runtime writers that fill matrix coeffs at $be6–$bf6
2. [ ] Resolve runtime targets of $12 / $c6 / $68a / $1a6 / $22c (need dynamic trace or overlay load map)
3. [ ] Find overlay sites that issue mode $1b for projected geometry
4. [ ] Identify exact perspective overlay routine (divs clusters at ~0xaa00, ~0xc000)
5. [ ] Bodies of other draw modes
6. [ ] Populate real object list from mission data once format known
7. [ ] Action clear body at $4c302 (may live in a different overlay)
8. [ ] Fuller action-structure layout beyond +8/+c/+10/+20

## Analysis Notes
- $34c=X0, $34e=X1, $350=Y0, $352=Y1 (after clipper sorts endpoints)
- Perspective: no divs in primary transform; divs abundant in overlays
- Clear wrappers at 0x4c0ba (create) and 0x4c15a (clear/destroy)
- Slots $8c/$90 = indices 35/36; created at init via $1a6/$22c
- Frame $1124 → object loop $72dc → transform $77d8 → draw 0x784c

## Bundle History
- fa18-port-043.1-action-create-slots-35-36-dc0339a.bundle (superseded)
- (next: 044.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
