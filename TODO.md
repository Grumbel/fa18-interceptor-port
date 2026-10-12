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
- [x] In-flight dumps (airborne + rockets); all dumps tracked under dumps/{preflight,inflight}/
- [x] Full symbol remap: static $xxxx → live $00Cxxxxx (docs/live_symbols.md)
- [x] Confirmed game state lives in Slow RAM $00C00000, not Chip low mem
- [x] Live $c6 → $00C03D46; $12 → RTS stub; matrix/world pos live in-flight
- [x] Static jsr $c6 is relocatable ref to offset $C6 in CODE hunk file 0x1EA04
- [x] $c6 function live at $00C25206: index×64 pool worker (docs/func_c6.md)


## Next Steps
1. [x] Label Euler angle0/1/2: roll (Rx) / pitch (Ry) / yaw (Rz)
2. [x] Resolve static jsr $c6 → object motion/physics step at $00C25206
3. [x] $c6 body: proximity cull, velocity damp ×15/16, position integrate
4. [x] Resolve jsr $59e → geometry/vertex helper at $00C25FAE
5. [x] Path-B/C and accept path fully traced; success returns d0=1
6. [x] Port $c6 motion step into C++ object loop (flight.hpp)
7. [x] Gate flags $18d/$17e: suppress object processing when nonzero
8. [x] $be/$c6 install vars are numeric scales at $00C44426/$2E (not ptrs)
9. [x] struct_A $00C01550 display/viewport hypothesis; struct_B secondary
10. [x] Pointer table writers: AllocMem → $00C16E22; field +$22 → $00C16E26
11. [x] Confirm copper/bitplane interpretation of $00C055F8
11b. [x] Writer of $00C16E1E: copy from $00C16D8A at $00C14C74; indexed table path
12. [x] Resolve $5ee → edge/span helper at $00C2E5D6
13. [x] Full $5ee path_hi/lo/tail; span_setup in draw.hpp
14. [x] Live $4202 mapped to $00C4B2CA
15. [x] $4202 = 12-byte XYZ vertex records; alternate perspective ×160/90 exact
16. [x] Overlay residency map: ~72/77 CODE hunks resident mid-flight
17. [x] Mode $01 DDA body + $16 walker decoded; $1b is stub-only
18. [x] Walker → perspective → $5ee chain complete
19. [x] Port walker+perspective into C++
20. [x] Wire walker_draw demo into main loop
21. [x] DDA modes $01/$03–$0a/$1f = pure setters; shared body $00C231F8
21b. [x] Modes $0f–$12/$26 also pure setters; real bodies need live $12 dump
21c. [x] DDA slope math $00C23318/$00C2340A (muls/divs + half-denom rounding)
21d. [x] Edge list $00C4ACC8 format (header,count, XYZ×N); C++ EdgeList
22. [x] Action create body at $00C4F010 decoded; clear partial
23. [x] Full action clear body at $00C4F3FE; table base $00C06460
23b. [x] Create 272=temp workspace, permanent object=64 (matches clear)
23c. [x] $00C06AA0=DosBase; init=Lock/UnLock/Examine; 64-byte object layout
24. [x] Runtime vectors $1a6/$22c → action hunk offsets
25. [x] $12 draw dispatcher → live $00C062C2 (rts stub in this dump)
26. [ ] Capture dump during active draw to see if $12 is repatched
27. [x] Live transform matrix/out addresses confirmed
28. [x] Builder A + B sequential; exact element formulas; dual sincos; C++ port already done
29. [x] Integrator $00C2C418: +$68-=d5, +$6A-=d6; clamps in 7200-circle units
29b. [x] Rates +$50..+$5A; filter $00C2C1F0; stick curve →+$50
29c. [x] Three rate writers ($50/$52/$54); curves by $62 mode
29d. [ ] Stick↔rate identity; rate2→angle2; label R/P/Y
29e. [x] C++ PlayerAttitude + filter_rates + integrate_attitude
29f. [x] Rate auto-center $00C2C476; input var map $4b0/$4b2/$448A0
30. [x] GfxBase $00C16E22; IntuitionBase $00C16D60; reg script $00C055F8
31. [x] Action dispatch via $00C4EBD8 + side tables; +0 not callable
31b. [x] $00C07478=AUD0-3 descriptors; $00C07488=action side table
31c. [x] Audio control API $00C4EBxx; DMACON/INTREQ bits; C++ AudioSystem
31d. [x] Sample block format; play $00C4ED00; PER/VOL helper
31e. [x] SetIntVector/AddIntServer; server $00C4ED80; interrupt-driven

## Analysis Notes
- Object pool: live base $00C449AA, stride 64, max 20, count $00C44EF7
- Object+0/+4/+8 = position; +12..16 and +24..28 = velocity/delta (two tiers)
- Object+$26 = flags (bits 4/5 control damping path); +$24 = active marker
- Matrix live at $00C44F4E (Q2.14); world X/Y/Z at $00C44ED0/$ED4/$ED8
- Current object ptr $00C16D6C → $00C44E2A
- $c6 = per-object physics step (cull + damp + integrate)
- Angle circle = 7200; table 900 words / 1800 bytes for 0–90°
- Object+0x22/0x24 hold per-object sin/cos
- Perspective: (x*160)/z+160, (y*90)/z+90
- All static abs <$2000 are relocatable into $00Cxxxxx Slow RAM
- $12 holds RTS stub when no draw mode active; real bodies are overlay/transient

## Bundle History
- fa18-port-052.1-overlay-map-drawmode-enum-dc0339a.bundle (superseded)
- fa18-port-053.1-matrix-writer-dynamic-trace-dc0339a.bundle (superseded)
- fa18-port-055.1-ram-dump-map-dc0339a.bundle (superseded)
- fa18-port-056.1-live-symbols-inflight-dumps-dc0339a.bundle (superseded)
- fa18-port-057.1-func-c6-object-worker-dc0339a.bundle (superseded)
- fa18-port-058.1-func-c6-body-outline-dc0339a.bundle (superseded)
- fa18-port-059.1-func-c6-helper-59e-paths-dc0339a.bundle (superseded)
- fa18-port-060.1-func-c6-path-bc-decompile-dc0339a.bundle (superseded)
- fa18-port-061.1-func-c6-motion-integrate-dc0339a.bundle (superseded)
- fa18-port-062.1-port-c6-motion-step-cpp-dc0339a.bundle (superseded)
- fa18-port-063.1-gate-flags-18d-17e-dc0339a.bundle (superseded)
- fa18-port-064.1-vector-slots-be-c6-data-dc0339a.bundle (superseded)
- fa18-port-065.1-func-5ee-edge-span-dc0339a.bundle (superseded)
- fa18-port-066.1-span-setup-cpp-dc0339a.bundle (superseded)
- fa18-port-067.1-live-4202-mapping-dc0339a.bundle (superseded)
- fa18-port-068.1-overlay-residency-map-dc0339a.bundle (superseded)
- fa18-port-069.1-action-create-live-dc0339a.bundle (superseded)
- fa18-port-070.1-vectors-1a6-22c-action-dc0339a.bundle (superseded)
- fa18-port-071.1-vector-12-rts-stub-dc0339a.bundle (superseded)
- fa18-port-072.1-mode-01-16-bodies-dc0339a.bundle (superseded)
- fa18-port-073.1-walker-perspective-5ee-dc0339a.bundle (superseded)
- fa18-port-074.1-walker-cpp-port-dc0339a.bundle (superseded)
- fa18-port-075.1-walker-demo-main-dc0339a.bundle (superseded)
- fa18-port-076.1-4202-xyz-perspective-dc0339a.bundle (superseded)
- fa18-port-077.1-euler-yaw-pitch-roll-dc0339a.bundle (superseded)
- fa18-port-078.1-be-c6-numeric-scales-dc0339a.bundle (superseded)
- fa18-port-079.1-struct-display-ctx-dc0339a.bundle (superseded)
- fa18-port-080.1-pointer-table-writers-dc0339a.bundle (superseded)
- fa18-port-081.1-copper-bitplane-00C055F8-dc0339a.bundle (superseded)
- fa18-port-082.1-action-clear-body-dc0339a.bundle (superseded)
- fa18-port-083.1-pointer-table-16e1e-writer-dc0339a.bundle (superseded)
- fa18-port-084.1-dda-shared-body-dc0339a.bundle (superseded)
- fa18-port-085.1-create-size-split-dc0339a.bundle (superseded)
- fa18-port-086.1-dosbase-action-layout-dc0339a.bundle (superseded)
- fa18-port-087.1-dda-slope-math-dc0339a.bundle (superseded)
- fa18-port-088.1-edge-list-format-dc0339a.bundle (superseded)
- fa18-port-089.1-attitude-struct-68-dc0339a.bundle (superseded)
- fa18-port-090.1-attitude-integrator-dc0339a.bundle (superseded)
- fa18-port-091.1-attitude-rate-filter-dc0339a.bundle (superseded)
- fa18-port-092.1-three-rate-axes-dc0339a.bundle (superseded)
- fa18-port-093.1-player-attitude-cpp-dc0339a.bundle (superseded)
- fa18-port-094.1-rate-autocenter-input-map-dc0339a.bundle (superseded)
- fa18-port-095.1-gfxbase-reg-script-dc0339a.bundle (superseded)
- fa18-port-096.1-action-dispatch-side-tables-dc0339a.bundle (superseded)
- fa18-port-097.1-audio-channel-table-dc0339a.bundle (superseded)
- fa18-port-098.1-audio-control-api-dc0339a.bundle (superseded)
- fa18-port-099.1-sample-block-play-dc0339a.bundle (superseded)
- fa18-port-100.1-audio-int-server-dc0339a.bundle (current tip)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
