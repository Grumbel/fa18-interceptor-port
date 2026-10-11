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
- [x] 0x1c1f4 is edge interpolation (DDA), not perspective; mode $01 = span/poly edge
- [x] C++ ActionTable mirroring create/dispatch for slots 35/36
- [x] Sole mode $1b issuer at 0x28274 (own CODE hunk); mode $16 companion; geometry path gap documented
- [x] True perspective at 0x2864a: (x*160)/z+160, (y*90)/z+90; vertex walker; $5ee handoff
- [x] Object structure +16/+28/+30/+32; $bb4 phase timer; $4b0/$4b2 attitude angles; $496 edge scratch
- [x] Matrix writer confirmed absent from static image; attitude func 0x404c; C++ mat_from_angles_fp

## Next Steps
1. [ ] Dynamic trace: breakpoint on write to $be6 to catch matrix writer
2. [ ] Resolve runtime targets of $12 / $c6 / $68a / $1a6 / $22c / $5ee / $a0dc / $a2f0
3. [ ] Bridge post-transform → vertex walker 0x2844e
4. [ ] Bodies of remaining draw modes in the 0x2826c CODE hunk
5. [ ] Populate real object list from mission data once format known
6. [ ] Action clear body; fuller structure layout
7. [ ] $4202 edge descriptor full layout

## Analysis Notes
- Matrix $be6–$bf6: zero absolute writers in entire binary; needs dynamic trace
- Attitude angles $4b0/$4b2 processed at 0x404c (no muls, no matrix stores)
- Object loop: jsr $a0dc/$a2f0 → bsr transform $7ad8 → post-transform $784c
- Perspective: (x*160)/z+160, (y*90)/z+90
- C++ mat_from_angles_fp() builds fixed-point matrix from 0–255 angles

## Bundle History
- fa18-port-049.1-object-struct-bb4-angles-dc0339a.bundle (superseded)
- (next: 050.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
