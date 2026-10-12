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
29. [~] $00C2C418 is servo (target−angle error), not rate integrator — see 29d
29b. [x] Rates +$50..+$5A; filter $00C2C1F0; stick curve →+$50
29c. [x] Three rate writers ($50/$52/$54); curves by $62 mode
29d. [~] +$66/68/6A Euler; $00C2C418 is servo not simple rate-int; rate→angle open
29g. [x] C++ JoyAxes + joy_update from JOY0DAT
29e. [x] C++ PlayerAttitude + filter_rates + integrate_attitude
29f. [x] Rate auto-center $00C2C476; input var map $4b0/$4b2/$448A0
30. [x] GfxBase $00C16E22; IntuitionBase $00C16D60; reg script $00C055F8
31. [x] Action dispatch via $00C4EBD8 + side tables; +0 not callable
31b. [x] $00C07478=AUD0-3 descriptors; $00C07488=action side table
31c. [x] Audio control API $00C4EBxx; DMACON/INTREQ bits; C++ AudioSystem
31d. [x] Sample block format; play $00C4ED00; PER/VOL helper
31e. [x] SetIntVector/AddIntServer; server $00C4ED80; interrupt-driven
31f. [x] object+0 = fib_Size; create body is file-size probe
32. [x] 5-plane 320×200 buffer $00C19648; palette mirror $00C19600
32b. [x] Plane-install $00C15494; BitMap ptr $00C19634; 3 plane banks
32c. [x] View $00C16D70 + ViewPort $00C16D82; InitVPort/MakeVPort; plane-clear
33. [x] Fix kAngleCircle redefinition (keep sole definition in transform.hpp)
34. [x] Decode register-script at $00C055F8 (6-byte reg/val/pad; palette + DIW + BPLPT)
35. [x] struct_A $00C01550 is static image data; pointer-table only copies it
36. [x] ColorMap $00C05EB8 confirmed; ColorTable $00C069F8 == script palette
37. [x] Sample-block queue: templates $00C06460 → holders $00C07488 → INTREQ via $00C16684
38. [x] Live copper list Chip $0000A400; BPLCON0 $0200→$4200 (4 planes)→$0200
39. [x] LOFCSR cprlist $00C01338 → start=$A400; InitVPort/MakeVPort/LoadView chain
40. [x] Alt register-script $540E8 = 32-colour variant; C++ palette + audio queue API
41. [x] Wire Amiga palette into SDL Flight clear/lines (draw_line_pal / clear_pal)
42. [x] Discrete yaw step at $00C2BF60 (fixed ±2000); continuous rate→angle still open
43. [x] C++ servo_attitude + discrete_yaw_step; live-dump handoff notes in TODO
44. [x] Fix ILBM row-interleave (panel corruption); attitude-driven runway view
45. [x] Flight axes from held keys (release → centre 128); ILBM decode smoke-ok
46. [x] Sky/ground fill from pitch; ILBM load diagnostics
47. [x] Object lines via gfx.pen palette (blitter_line_stub → draw_line_pal)
48. [x] README controls/status; AudioSystem hooked on state transitions
49. [x] Mesh format from dump ($00C39B52/$00C3A58C); mesh.hpp + flight draw
50. [x] Model cluster $39700–$3A600 catalogued; verts beyond N; aircraft edges
51. [x] Opcode heuristic (4xxx/8–Dxxx vs E–Fxxx coords); mesh_parse(); more aircraft meshes
52. [x] DL two-level: offset table → sub-lists; byte-offset/6 vert indices; mesh_edges_from_dl()
53. [x] $5400 slot = LOD counter d5 (0..9) from path-C; high-byte DL command taxonomy
54. [x] Proximity threshold 320 (asr#8); LOD countdown d5=8..1; companion stride-256 records
55. [x] Full cluster record layout (7FFF/tags/hull/verts/DL); Mesh.hull + draw_hull_2d()
56. [~] $00C2FF10 is radar scope pair-walker but data is fixed overlay ($00C4D454), not mesh hull
57. [x] DL sub-list edge cmds refined: 800C begin, 8000 end, C00C polyline
58. [x] Offset-table base = DL start (byte offsets); mesh_edges_from_dl() fixed
59. [x] Top-level DL: 4070/40PP/C016 pattern; 80LL = end-of-DL offset; Axxx reloc pointers
60. [x] Companion runtime-written ($00C1CD88); edge cmds 800C begin / 8000 end / 8018+attrs
61. [x] $5400 pointers = hull starts; $00C1CD38 transforms hull → companion
62. [x] Companion transform: asl scale (d7) + translate (d2/d4) + $3FFF mask; attr hi/4 colour?
63. [x] mesh_edges_from_dl skips end-attrs; $5400=hull note in mesh.hpp; $70 imm is radar not DL
64. [x] Mesh.edge_pens + draw_mesh per-edge colour from attr hi/4 (hypothesis)
65. [x] Axxx → runtime mesh-instance pool ($00C43700+); slot table at $00C21400
66. [x] Instance stride $6A; tag $04D0; ptrs +46/+78; handler $00C08232 = geom walker
67. [x] Instance pool static (preflight ≡ inflight); pre-built templates not runtime expand
68. [x] Walker cmd class: bit15=edge, bit14=$4xxx skip-next, low14/8=threshold
69. [x] Pen from separate a2 stream (not mesh attrs); $5ee copies pen then draws
70. [x] Object→drawable table $00C21000; 19 instances; 3977A/39E42/etc mapped
71. [x] LOD tables ($00C3A47E/$00C3A576): count+ptrs+hull+mesh; $00C1D8A0 trampoline
72. [x] PP on ADF (static); walker uses only bit14 of $4xxx; $100B attrs cluster-only
73. [x] Mesh pipeline overview diagram in mesh_format.md
74. [x] 39738 edges from dump sub-lists (18 unique); edge_pens from attr_hi/4
75. [x] 39800/398C6/39E00 dump-derived edges + edge_pens
76. [x] 39B66 (5 edges) + 3A4A8 (4-cycle) dump-derived edges
77. [x] mesh_edges_from_dl: Axxx/80LL offset-table pattern (handles small offsets)
78. [x] Link-graph DL: $80xx lo≥$30 = intra-DL offset; parser follows links
79. [x] 39980 C0C0-style aircraft edges + pens; wired into Flight demo
80. [x] mesh_edges_from_dl: always full C00C scan + offset targets; shared-sublist note
81. [x] PP/$70 observations table; mesh_from_words helper; hull radar in Flight demo
82. [x] Walker bit14-skip worked example; structure_3A4A8 in Flight demo
83. [x] DL expansion step: walker a2 points at expanded 800C stream, not raw PP DL
84. [x] Cmd jump table $00C1E840; 4070→nop; 800C→edge builder; PP not a dispatch key
85. [x] Two dispatch paths (walker vs jump-table); edge-list builders $84BA family
86. [x] mesh_from_dump_file() + CMake dumps copy; demo auto-load $00C39710
87. [x] Fix mesh_from_words declaration order (after mesh_edges_from_dl)
88. [x] Alternate DLs at $37xxx are pre-built (drawable map $20B94), not runtime-expanded
89. [x] Drawable map entries catalogued; structure_3A980; cluster 7FFF record list
90. [x] Drawable selection path ($4D6E2/$4DD72 tables); $21618 pre-walker; dump trim
91. [x] Scene list stride $18: flag + dispatch-slot ptr → handler/stream LOD
92. [x] Scene record fields (flag type/LOD, coords, type_tag 3F44); C++ struct
93. [x] scene_list_from_dump() + Flight demo logs scene list A
94. [x] DispatchSlot + dispatch_slot_from_dump; demo resolves handler/stream
95. [x] Scene→stream→cluster mesh auto-load; draw up to 4 in Flight demo
96. [x] Scene coords as Q8.8 XZ; demo positions meshes from scene list
97. [x] Instance-pool format ($43xxx); mesh_instance_4335E in demo
98. [x] mesh_from_instance_dump auto-loader; list-B streams in demo

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


## Needs live dump / dynamic evidence

### Priority dumps (FS-UAE console debugger)
1. **Stick-hold attitude** — enter flight, hold stick full forward 2s, dump:
   - `$00C44EBC` length `$80` (PlayerAttitude: rates +$50, angles +$66)
   - registers at `$00C2C418` entry (d5/d6 = servo targets)
   - `$00C44818` / `$00C4481A` (input axes $4b0/$4b2)
2. **One-axis polarity** — same but only X then only Y stick, compare angle deltas
3. **Active draw** — dump `$00C062C2` ($12 vector) while geometry is drawing (not RTS)
4. **PCM** — dump Chip sample range from a known template LC (e.g. `$0000B3E8` len `$35FA`)

### Open items
- [ ] continuous rate → angle path (servo targets d5/d6; discrete step at $00C2BF60)
- [ ] d5/d6 target angle source into servo
- [ ] JOY accumulators → desired-attitude / matrix weights
- [ ] Chip-RAM sample PCM capture during audio playback
- [ ] `$12` draw-mode body while active (currently RTS stub)
- [ ] One-axis stick hold to label roll vs pitch polarity

## Static-analyzable remaining
- [x] Register-script format + copper path (MakeVPort/LoadView) done
- [x] View/ViewPort + plane-clear + ColorMap + live copper list done
- [x] Bootstrap of `$00C01550` pointer table: static data (resolved)
- [x] Sample-block queue sites from game events (resolved)
- [x] BPLCON0 `$0200`/`$4200` explained (4 active planes; 5th buffer unused here)
- [x] LOFCSR cprlist field map confirmed (start=$A400, MaxCount=42)
- [x] Alternate register-script at `$540E8` = 32-colour mode template
- [x] Mesh pointer table `$5400`/`$00C4C4C8` + model cluster catalogued
- [x] Vertex stream continues past declared N until $40xx/$80xx/$C0xx opcodes
- [x] Opcode heuristic (exclude E000–FFFF small negatives); mesh_parse() + raw DL retained
- [x] Additional aircraft meshes 398C6 / 39E00 / 39F66 + structure 3A4A8 in port
- [x] DL sub-list edge encoding: byte-offset/6 indices after 8xxx cmds; mesh_edges_from_dl()
- [x] `$5400` slot = LOD/proximity d5 (0..9), not object-type
- [x] Top-level high-byte command taxonomy (40/41–45/C0/A0–A2/80–83)
- [x] Proximity threshold 320 after asr#8 (`$00C446DE`/`$00C446E0`)
- [x] LOD countdown path: moveq #8,d5 / subq / bgt
- [x] Companion records: `$00C4C4F4 + slot×256 + 2`, match obj+0x30/+0x32 ±1
- [x] Full cluster record: 7FFF sep, tags 0100/0200, 6-pair 2D hull, verts, DL
- [x] Mesh.hull + draw_hull_2d(); mesh_parse handles full records
- [~] `$00C2FF10` radar pair-walker uses fixed overlay `$00C4D454`, not mesh hull
- [x] DL sub-list cmds: `$800C` begin edges, `$8000` end group, `$C00C` polyline
- [x] Offset-table base = DL start (byte offsets from first opcode word)
- [x] Top-level pattern `4070 40PP C016 …`; `80LL` = end-of-DL byte offset
- [x] `Axxx` → runtime mesh-instance pool (`$00C43700`+); stride `$6A`, tag `$04D0`
- [x] Instance: ptr A = base+46 (sub-hdr), ptr B = base+78 (N + coord pairs)
- [x] Slot table `$00C21400` → static DLs or instance-pool entries
- [x] Handler `$00C08232` = geometry walker (`link a6,#-152`, same as `$00C1D96C`)
- [x] Instance pool **static** (preflight ≡ inflight); **19 entries**
- [x] Object→drawable table `$00C21000`–`$00C21500`: 3D DLs / LOD tables / instances / walkers
- [x] 3D DL examples: `$00C3977A` (39738), `$00C39842`, `$00C398FC`, `$00C39E42`
- [x] LOD table: count + ptrs into mesh body + hull + FFFF/N/verts/DL
- [x] `$00C1D8A0` = trampoline (`bra.w $00C1D96C`)
- [x] Companion runtime-written at `$00C1CD88` (projected hull / attrs from a0)
- [x] Edge cmds: `$800C` begin+verts, `$8000` end, `$8018`/`$1E`/`$24`/`$2A` end+attrs
- [x] `$5400` pointers = **hull starts**; `$00C1CD38` is the hull→companion transform
- [x] Companion transform: `asl.l d7` scale, add d2/d4 translate, `andi #$3FFF`
- [~] Attr word: hi/4 = colour candidate (0..7), lo = style; `$100B` most common
- [ ] Exact meaning of group param PP (`$22`/`$20`/`$BA`) and setup `$70`
      (ADF-static; walker tests only bit14 of `$4xxx`; low-byte consumer not located)
- [x] `$100B` attrs are 3D-cluster-only (12×); instance pool uses simpler edges (0×)
- [x] Walker classification: bit15=edge, bit14=skip-next, (w&$3FFF)/8=threshold
- [x] Link-graph DL (39B66): `$80xx`/`$C0xx` lo≥`$30` = intra-DL byte-offset link
- [x] mesh_edges_from_dl follows Axxx offset tables + link-graph targets
- [~] Attr hi/4 → colour: wired into Mesh.edge_pens; **unconfirmed** — pen comes from
      a separate a2 draw-command stream (`move.w (a2)+, pen`), not mesh edge attrs
- [x] `$5ee` copies pen `$324`→`$326` then draws; pen set before the call
- [x] mesh_edges_from_dl consumes/skips end-with-attrs trailing words
- [x] Mesh.edge_pens parallel array; draw_mesh uses per-edge pen when present
- [x] 39980 C0C0-style aircraft: dump-derived edges + edge_pens; demo slot
- [x] mesh_edges_from_dl always full-scans C00C + follows offsets; shared-sublist note
- [x] PP/$70 observation table (40PP vs 60xx); mesh_from_words; hull radar demo
- [x] Walker bit14-skip worked example (4070 skips 4022); structure_3A4A8 demo
- [x] DL expansion: a2=$00C37DD8 is flat 800C stream; PP consumed before walker
- [x] Cmd jump table $00C1E840 (byte-index); 4070=nop, 800C=edge builder
- [x] Two dispatch paths; edge-list builders $84BA/$8512/$855E → $4ACC8
- [x] mesh_from_dump_file loads cluster records from inflight CODE dump
- [x] mesh_from_words moved after mesh_edges_from_dl (fix compile)
- [x] $37xxx alternate DLs pre-built via drawable map $20B94; PP still open
- [x] Drawable map $20B94 entries; structure_3A980; 8 cluster 7FFF records listed
- [x] Selection via $4D6E2/$4DD72 + $446E2 index; $21618 flag gate; dump auto-trim
- [x] Scene list stride $18 (flag, dispatch ptr); links to $21000 LOD slots
- [x] Scene record field map + SceneDrawableRecord; stream $39690 noted
- [x] scene_list_from_dump loads list A/B; demo logs flag/type/lod/coords
- [x] DispatchSlot resolves handler+streams; demo logs full scene→stream path
- [x] Scene streams mapped to cluster records; auto-load+draw in demo
- [x] Scene coord_a/b as Q8.8 XZ (hypothesis); demo places meshes
- [x] Instance-pool $43xxx format; mesh_instance_4335E
- [x] mesh_from_instance_dump; list-B instance streams auto-loaded

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
- fa18-port-100.1-audio-int-server-dc0339a.bundle (superseded)
- fa18-port-101.1-joy0dat-stick-path-dc0339a.bundle (superseded)
- fa18-port-102.1-rate-matrix-projection-dc0339a.bundle (superseded)
- fa18-port-103.1-object0-filesize-dc0339a.bundle (superseded)
- fa18-port-104.1-roll-pitch-labels-dc0339a.bundle (superseded)
- fa18-port-105.1-angle66-matrix-path-dc0339a.bundle (superseded)
- fa18-port-106.1-servo-not-integrator-dc0339a.bundle (superseded)
- fa18-port-107.1-todo-handoff-cleanup-dc0339a.bundle (superseded)
- fa18-port-108.1-display-buffers-5plane-dc0339a.bundle (superseded)
- fa18-port-109.1-plane-install-banks-dc0339a.bundle (superseded)
- fa18-port-110.1-view-viewport-plane-clear-dc0339a.bundle (superseded)
- fa18-port-111.1-fix-kAngleCircle-redef-dc0339a.bundle (superseded)
- fa18-port-112.1-reg-script-decode-dc0339a.bundle (superseded)
- fa18-port-113.1-structA-static-colormap-dc0339a.bundle (superseded)
- fa18-port-114.1-sample-block-queue-dc0339a.bundle (superseded)
- fa18-port-115.1-live-copper-bplcon0-dc0339a.bundle (superseded)
- fa18-port-116.1-lofsr-cprlist-display-init-dc0339a.bundle (superseded)
- fa18-port-117.1-palette-cpp-audio-queue-dc0339a.bundle (superseded)
- fa18-port-118.1-sdl-palette-flight-dc0339a.bundle (superseded)
- fa18-port-119.1-discrete-yaw-step-dc0339a.bundle (superseded)
- fa18-port-120.1-servo-cpp-dump-handoff-dc0339a.bundle (superseded)
- fa18-port-121.1-ilbm-fix-runway-view-dc0339a.bundle (superseded)
- fa18-port-122.1-axis-hold-center-dc0339a.bundle (superseded)
- fa18-port-123.1-sky-ground-ilbm-log-dc0339a.bundle (superseded)
- fa18-port-124.1-object-pen-palette-dc0339a.bundle (superseded)
- fa18-port-125.1-readme-audio-hook-dc0339a.bundle (superseded)
- fa18-port-126.1-mesh-from-dump-dc0339a.bundle (superseded)
- fa18-port-127.1-mesh-cluster-aircraft-dc0339a.bundle (superseded)
- fa18-port-128.1-mesh-parse-more-aircraft-dc0339a.bundle (superseded)
- fa18-port-129.1-dl-sublist-byteoffset-edges-dc0339a.bundle (superseded)
- fa18-port-130.1-lod-slots-dl-taxonomy-dc0339a.bundle (superseded)
- fa18-port-131.1-lod-threshold-companion-records-dc0339a.bundle (superseded)
- fa18-port-132.1-cluster-record-hull-2d-dc0339a.bundle (superseded)
- fa18-port-133.1-radar-hull-consumer-dc0339a.bundle (superseded)
- fa18-port-134.1-dl-sublist-refine-radar-correct-dc0339a.bundle (superseded)
- fa18-port-135.1-offset-table-dl-base-dc0339a.bundle (superseded)
- fa18-port-136.1-toplevel-dl-pattern-80ll-dc0339a.bundle (superseded)
- fa18-port-137.1-companion-runtime-edge-attrs-dc0339a.bundle (superseded)
- fa18-port-138.1-slot-ptrs-are-hull-starts-dc0339a.bundle (superseded)
- fa18-port-139.1-companion-transform-attr-colour-dc0339a.bundle (superseded)
- fa18-port-140.1-edge-attr-skip-hull-ptr-note-dc0339a.bundle (superseded)
- fa18-port-141.1-edge-pens-from-attr-dc0339a.bundle (superseded)
- fa18-port-142.1-axxx-runtime-instances-dc0339a.bundle (superseded)
- fa18-port-143.1-instance-stride-handler-dc0339a.bundle (superseded)
- fa18-port-144.1-instance-pool-is-static-dc0339a.bundle (superseded)
- fa18-port-145.1-walker-cmd-classification-dc0339a.bundle (superseded)
- fa18-port-146.1-pen-from-draw-stream-dc0339a.bundle (superseded)
- fa18-port-147.1-object-drawable-table-dc0339a.bundle (superseded)
- fa18-port-148.1-lod-tables-trampoline-dc0339a.bundle (superseded)
- fa18-port-149.1-pp-static-attrs-cluster-only-dc0339a.bundle (superseded)
- fa18-port-150.1-mesh-pipeline-overview-dc0339a.bundle (superseded)
- fa18-port-151.1-39738-dump-edges-pens-dc0339a.bundle (superseded)
- fa18-port-152.1-more-aircraft-dump-edges-dc0339a.bundle (superseded)
- fa18-port-153.1-39B66-3A4A8-dump-edges-dc0339a.bundle (superseded)
- fa18-port-154.1-offset-table-parser-axxx-dc0339a.bundle (superseded)
- fa18-port-155.1-link-graph-dl-parser-dc0339a.bundle (superseded)
- fa18-port-156.1-39980-c0c0-edges-pens-dc0339a.bundle (superseded)
- fa18-port-157.1-mesh-dl-fullscan-shared-subs-dc0339a.bundle (superseded)
- fa18-port-158.1-pp-obs-mesh-from-words-hull-demo-dc0339a.bundle (superseded)
- fa18-port-159.1-walker-skip-worked-example-3a4a8-dc0339a.bundle (superseded)
- fa18-port-160.1-dl-expansion-step-a2-stream-dc0339a.bundle (superseded)
- fa18-port-161.1-cmd-jump-table-e840-dc0339a.bundle (superseded)
- fa18-port-162.1-two-dispatch-paths-edge-builders-dc0339a.bundle (superseded)
- fa18-port-163.1-mesh-from-dump-file-loader-dc0339a.bundle (superseded)
- fa18-port-164.1-fix-mesh-from-words-order-dc0339a.bundle (superseded)
- fa18-port-165.1-alternate-dl-prebuilt-drawable-map-dc0339a.bundle (superseded)
- fa18-port-166.1-drawable-map-structure-3a980-dc0339a.bundle (superseded)
- fa18-port-167.1-drawable-selection-path-dump-trim-dc0339a.bundle (superseded)
- fa18-port-168.1-scene-list-stride18-dispatch-link-dc0339a.bundle (superseded)
- fa18-port-169.1-scene-record-fields-struct-dc0339a.bundle (superseded)
- fa18-port-170.1-scene-list-from-dump-loader-dc0339a.bundle (superseded)
- fa18-port-171.1-dispatch-slot-from-dump-dc0339a.bundle (superseded)
- fa18-port-172.1-scene-stream-mesh-autoload-dc0339a.bundle (superseded)
- fa18-port-173.1-scene-coords-q88-positioning-dc0339a.bundle (superseded)
- fa18-port-174.1-instance-pool-mesh-4335e-dc0339a.bundle (superseded)
- fa18-port-175.1-instance-dump-loader-listb-dc0339a.bundle (current tip)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
