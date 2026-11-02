# Cross-project comparison: retro-foundry/fa18interceptor

Examined 2026-11-02:

- This port: https://github.com/Grumbel/fa18-interceptor-port (`227fa8f`)
- Independent analysis: https://github.com/retro-foundry/fa18interceptor
  (`a8fc6779`)

## Bottom line

The other tree is **not** a second working C++ port. Its own README says it
aims for byte-exact, symbolic 68000 reconstruction against a pinned Engine9000
Amiga run. It contains bounded instruction traces, raw Ghidra P-code, named
68k assembly slices, and analyses of keyboard, flight and graphics routines.
This port already runs the complete game via recompilation/interpreter and has
verified native C++ for many of the same functions. Importing its assembly
would not by itself complete or accelerate this port.

Its most reusable contribution is **independent evidence for specific code
paths**. The two projects' runtime addresses differ, but both preserve the
original 185-Hunk executable layout. The reference project published a
relocation-verified map from its runtime addresses to original Hunk indices and
offsets. Our `tools/loadseg.py` gives every Hunk a fixed payload address.

## Address crosswalk

Use `tools/reference_addr.py` with an external checkout of the reference:

```sh
git clone https://github.com/retro-foundry/fa18interceptor.git ../fa18interceptor
python3 tools/reference_addr.py ../fa18interceptor --from-reference 0xc1718e 0xc2fa7e
python3 tools/reference_addr.py ../fa18interceptor --from-port 0x292e6
python3 tools/reference_addr.py ../fa18interceptor --index
```

`--index` translates the addresses in its `analysis/routines/c*.md` filenames
and includes a linkable path to each note. At the examined commit, **all 159
address-named routine notes map to Hunk ranges marked `verified`**. The tool
does not copy or vendor its large trace files or reconstructed binary bytes.
To annotate those addresses in our Ghidra project without replacing existing
function names, see `tools/ghidra/README.md` and
`tools/ghidra/ImportRetroFoundryNotes.java`.

The translation is:

```text
reference address - reference Hunk payload base = Hunk offset
port address = tools/loadseg.py Hunk payload base + Hunk offset
```

Selected independent correspondences:

| Reference live address | Hunk + offset | Our address | Role / status here |
| --- | --- | --- | --- |
| `$C0EFD4` | 0 + `$1124` | `$2112C` | Game-frame / state update; native |
| `$C1342C` | 0 + `$557C` | `$25584` | Control surfaces; native `controls.cpp` |
| `$C1718E` | 0 + `$92DE` | `$292E6` | JOY0DAT mouse callback; known HLE |
| `$C1AD74` | 5 + `$15C` | `$2DAD0` | Assembly command dispatcher; original |
| `$C1B410` | 5 + `$7F8` | `$2E16C` | Three signed control-byte axes; original |
| `$C1B58E` | 5 + `$976` | `$2E2EA` | Comma/period rudder input masks; original |
| `$C1BB7A` | 5 + `$F62` | `$2E8D6` | Weapon selection; original |
| `$C1BD04` | 5 + `$10EC` | `$2EA60` | F1–F10 throttle levels; original |
| `$C1D91A` | 8 + `$1652` | `$30872` | Scene projection; native |
| `$C254E8` | 20 + `$738` | `$39A54` | Heading octant; native `flight.cpp` |
| `$C2E6DA` | 32 + `$12D2` | `$42EBA` | Sine/cosine lookup; native `flow.cpp` |
| `$C2FA7E` | 36 + `$5EE` | `$44686` | Blitter line preparation / submission |
| `$C2FD8C` | 36 + `$8FC` | `$44994` | Bitplane-4 blit submission |
| `$C2FF48` | 36 + `$AB8` | `$44B50` | DMA + display submission; our 2D-fill hook |
| `$C304B2` | 36 + `$1022` | `$450BA` | Short blitter setup leaf |
| `$C46184` | 71 + `$B54` | `$5B47C` | 512-byte aircraft/control-record array |

Last row maps through a runtime-*mutated* DATA segment rather than a byte-
verified CODE segment; the Hunk base was inferred from other verified
relocations. Reference variable addresses in that segment are specific to its
snapshot; the mapped Hunk offsets are more portable than its absolute values.

The two projects used different Amiga disk/executable variants. A matching
Hunk layout does **not** guarantee matching machine-code bytes. Use the
optional `--executable /path/to/F-18\ Interceptor` to verify that our Hunk
sizes and kinds match the reference inventory; then compare disassembly at
the relevant offsets before adapting an algorithm. This option cannot prove
byte identity. The reference notes occasionally contain stale Hunk-offset
labels (for example its `$C1BD04` note calls it Hunk 5 + `$5F5C`, while the
verified map gives Hunk 5 + `$10EC`). Prefer the machine-readable mapping.

## Highest-value follow-ups

1. **Assembly keyboard/flight input, Hunk 5 (`$2DAD0-$2F019`).** The other
   project has a contiguous, byte-exact reconstruction of the dispatch tables
   and useful isolated key traces (`analysis/keyboard_command_dispatch.md`).
   Our hunk-0 C input path is translated, but much of this hunk-5 dispatcher
   still runs through generated 68k. Strong candidates for *small, verified*
   native replacements are the rudder selector `$2E2EA`, three-axis updater
   `$2E16C`, and F1–F10 throttle-level update `$2EA60`. Its notes also
   document release/reset routes, context gates, and the fire/weapon key
   queue. This may improve future gamepad-analogue and overlay help work.
   `docs/hunk5_input.md` cross-checks the physical raw-key routes, including
   misleading key names in the other project's assembly.

2. **Line blitter and plane submits, Hunk 36 (`$44686-$451B2`).** The
   reference reconstructs line parameter preparation, hardware waits,
   per-plane blits and small submission leaves (`analysis/routines/` and
   `source_amiga/observed/`). `docs/retro_foundry_hunk36.md` now ties the
   reference's `$C2FF48` wrapper to our `$44B50` polygon path and matches
   the `$C2FD8C` four-plane packet to our `$44994` backdrop blits. It also
   identifies a possible negative-mode line-colour mismatch (`$5AC4F` versus
   `$5AC4C`) in the *hi-res observer*. This needs original-ADF verification
   before changing any native routine; the independent assembly is evidence,
   not a drop-in replacement for the renderer.

3. **Separate validation of flight/control structure.** The reference's
   `analysis/control_record_layout.md` confirms the 512-byte stride and
   directional fields at record offsets `$28-$2A`, matching our aircraft array
   at `$5B47C` and `src/native/controls.cpp`. Its angle-octant, trig and
   projection notes independently align with existing native routines. Use
   these as regression oracles, not new implementation work.

4. **Runtime trace methodology.** Their deterministic Engine9000 snapshots,
   register traces and custom-chip write logs offer an independent way to
   reproduce edge cases. Our existing native equivalence gate is stronger for
   changes in this codebase, because it compares whole-game memory and
   per-call behavior. If a trace disagrees, first compare executable variants
   and input/timing setup before trusting either address annotation.

## What it does not currently solve

- No evidence of a more complete native engine, mission/AI model or terrain
  database than our `docs/world_format.md` and `docs/missions.md`.
- No solution to the fixed 15-fps simulation loop or GL interpolation.
- No ready-to-use sprites/cockpit UI asset extraction: their
  `analysis/cockpit_bitplane_assets.md` covers mutable 320x200 display planes,
  not a clean static asset library.
- It does not justify committing third-party reconstructed machine-code bytes
  or original game assets to this repository. Use independently described
  behavior and verify it with the original game and `tests/check_native.sh`.

In short, this source should guide targeted RE and testing, not a wholesale
merge. The address-index tool makes that guidance searchable from our native
sources and TODO list.
