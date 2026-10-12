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
- [x] Complete 45-mode catalogue; $c6 dynamic install via table $151a / index $220
- [x] Overlay/HUNK map; action clear body past binary end (separate overlay); DrawMode enum expanded
- [x] Object pool: base $642, stride 64 bytes, max 20; active count $b8f
- [x] Dynamic trace: matrix writer located (live builders @ 00C2D07A / 00C2CFE0)
- [x] Sin/cos table @ 00C3D320 (897 entries, 0°–90°, Q2.14, 0x4000=1.0)
- [x] Builder uses asr #14; transform multiply still asr #6
- [x] ADF load deltas: CODE +$00C0CA08, data +$00C44368 (session-specific)
- [x] Angle units: 7200 = 360° (20 units/degree); quadrant constants 1800/3600/5400/7200
- [x] Trig helper 00C2D232: byte-indexed table, quadrant fold + sign
- [x] Builder A (00C2CFD2) vs B (00C2D06C) entry points and call sites
- [x] Object fields +$22/+24 = sin/cos Q2.14 (written by 00C2C8A6)
- [x] Builder A+B sequential via orchestrator 00C2C4B2 (A1 += $92 between)
- [x] Frame path calls matrix orchestrator via 00C2C4F4
- [x] Exact 3×3 element formulas for builders A and B
- [x] Helper 00C2D14E dual sincos; Euler angle→register map
- [x] C++ builder_A/B + mat_from_angles_q14 in transform.hpp
- [x] Transform confirmed to read global $be6 (not the +$92 secondary)
- [x] Flight demo primary cube uses draw_object_fp / mat_from_angles_q14
- [x] Object struct: +$22/+$24 Q14 sin/cos, +$26 type flags
- [x] Post-transform 0x784c: state gate, jsr $c6, set state=2
- [x] CMake install() rules for Nix packaging
- [x] $be/$c6 sibling install from table $151a; no static $12/$5ee stores
- [x] Geometry hunk pipeline: walker 0x2844e → perspective 0x2864a → jsr $5ee
- [x] Transform world store: $422+$b68→obj+0, Y/Z similar; $4202 clip fields +6/+8/+$a
- [x] Attitude 0x404c is control decoder (not matrix); $4b0 stays 8-bit input
- [x] Full hunk inventory: 68 CODE; confirmed no static $12/$5ee stores in any hunk
- [x] Port: apply_view_masked ($3fffff), ObjectList, state=2 after draw_object_fp
- [x] Port: $4b0-style axes + ObjectList flight demo (arrows → Euler Q14)
- [x] Map session RAM dumps (fa18_{low,code,data,stack}.bin) to ADF binary
- [x] Primary CODE relocation base for this dump: live = $00C06A70 + runtime_r
- [x] dump_offset = 0xA08 + runtime_r; formulas + tool tools/map_ram_dumps.py
- [x] Confirm dump taken pre-flight (vectors $12/$5ee/$c6 still zero)
- [x] Document mapping in docs/ram_dump_map.md


## Next Steps
1. [ ] Label Euler angle0/1/2 (yaw/pitch/roll); $4b0 is separate 8-bit input
2. [ ] Resolve $12/$5ee targets – need a dump with active objects (current dump has them zero)
3. [ ] Live $c6 target (same – requires active-object dump)
4. [ ] Bodies of remaining draw modes (beyond $01/$16/$1b)
5. [ ] Action clear body (overlay past file end)
6. [ ] $4202 full layout beyond clip bounds +6/+8/+$a and (a1)+ stream
7. [ ] Disassemble live primary CODE (transform / matrix builders) from fa18_code.bin using the new map
8. [ ] Map remaining resident overlay hunks once a fuller snapshot is available
9. [x] Builder A + B sequential; exact element formulas; dual sincos; C++ port already done

## Analysis Notes
- Object pool: base $642, stride $40 (64), max 20, count $b8f
- Matrix writer in runtime CODE; two builders share table + asr#14
- Angle circle = 7200; table 900 words / 1800 bytes for 0–90°
- Object+0x22/0x24 hold per-object sin/cos
- Perspective: (x*160)/z+160, (y*90)/z+90
- Runtime vectors $12/$5ee still zero while no objects active
- **This dump:** primary CODE at $00C06A70; secondary abs base $00C0CA08; many overlays not resident

## Bundle History
- fa18-port-052.1-overlay-map-drawmode-enum-dc0339a.bundle (superseded)
- fa18-port-053.1-matrix-writer-dynamic-trace-dc0339a.bundle (superseded)
- fa18-port-055.1-ram-dump-map-dc0339a.bundle (current tip)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
