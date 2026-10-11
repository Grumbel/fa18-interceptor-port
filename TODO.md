# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] ADF extraction, scaffolding, SDL2 skeleton, Hunk scanner
- [x] String extraction, library/path/credits documentation
- [x] Early init disassembly (Forbid → OpenLibrary → $30)
- [x] High-level flow + interactive C++ state machine
- [x] Absolute-address calling convention documented
- [x] JOTD WHDLoad slave imported; PL_ sites mapped
- [x] flake.nix added
- [x] Protection / empty-loop / keyboard / VBL / picture-wait sites documented
- [x] Blitter / custom-chip region located (0x288c8–0x2aa30)
- [x] Identified low-memory jump-table architecture (~197 vectors < $1000)

## Next Steps
1. [ ] Locate the code that builds / fills the jump table
2. [ ] Resolve a few key vectors ($1c, $ef4, $996, $cce, $84, …) so
      picture-wait and timing paths become fully traceable
3. [ ] Disassemble blitter region for bitplane pointers / polygon fill
4. [ ] Trace keyboard test (0x1590) into menu logic
5. [ ] Expand C++ state machine with ILBM / placeholder rendering
6. [ ] Identify main flight loop and 3D transform entry points
7. [ ] Begin RE of 3D math

## Analysis Notes
- Most calls are `jsr $xxxx.L` into a runtime jump table in low memory
- PL_ offsets == file offsets for patches *inside* the code segment
- Blitter-wait loops at 0x28800+ = graphics drawing code
- splsh empty on disk; title graphics inside binary or procedural

## Bundle History
- fa18-port-012.1-… (superseded)
- (next: 013.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
