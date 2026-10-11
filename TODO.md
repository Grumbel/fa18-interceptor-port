# TODO - FA-18 Interceptor Port

## Current Tip
dc0339a - Initial project setup

## Completed
- [x] Extracted original ADF (FA-18 Interceptor (1988)(Electronic Arts).adf)
- [x] Catalogued main files: executable `F-18 Interceptor` (311832 bytes, Amiga Hunk), pix/ (splsh/frnt5/inst5 IFF?), text/ (9 files), config
- [x] Project scaffolding (AGENTS.md, README, .gitignore, assets/)

## Next Steps (priority order)
1. [ ] Properly parse Amiga Hunk / detect if packed (header parse incomplete; table_size=185 suspicious, first=last=0)
2. [ ] Identify packer (Imploder? PowerPacker? custom) and unpack the executable
3. [ ] Convert IFF images in assets/pix to PNG for inspection (use ifflib or custom)
4. [ ] Dump strings from binary and text/ files
5. [ ] Locate WHDLoad slave source by JOTD (has disassembly notes) and any public RE notes from EAB
6. [ ] Set up CMake + SDL2 skeleton that can display the splash image
7. [ ] Static analysis: entry point, OS calls (OpenLibrary etc.), custom chip registers

## Analysis Notes
- Binary starts with standard HUNK_HEADER 0x000003F3
- Appears to have only 1 hunk declared but size field looks wrong; may be overlay or compressed
- From EAB: mostly assembly, high-level (init, music) in C; uses Bresenham for circles (sun, pilot head)
- Codewheel protection present on original
- Preferred crack for study: Unit A (has HD install, x2 binary)

## Open Questions
- Public disassembly available?
- Terrain data format (procedural SF Bay?)
- Sound format (ProTracker / custom?)

## Bundle History
- fa18-port-001.1-initial-setup-dc0339a.bundle (this)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
