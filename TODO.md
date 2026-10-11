# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] Extracted original ADF
- [x] Catalogued main files
- [x] Project scaffolding
- [x] Confirmed binary is multi-hunk **overlay** executable (261+ hunk markers, HUNK_BREAK/OVERLAY present). Not a simple single-hunk packed file.
- [x] Converted available ILBM assets (frnt5, inst5) to PNG (small HUD/font-like images; splsh is empty on disk)
- [x] Basic Amiga Hunk scanner (CODE/DATA) in C++
- [x] CMake + SDL2 skeleton that loads the executable and opens a window

## Next Steps (priority order)
1. [ ] Improve Hunk parser to handle the overlay table / HUNK_HEADER properly and reconstruct loadable segments
2. [ ] Extract & decode more graphics from binary (planar bitmaps, copper lists, etc.)
3. [ ] String dump / identify UI text and mission data
4. [ ] Locate and study JOTD WHDLoad slave source / any public disassembly (EAB zone material)
5. [ ] Identify entry point and initial AmigaOS calls (OpenLibrary("graphics.library") etc.)
6. [ ] Begin mapping 3D engine / transformation routines
7. [ ] Asset pipeline: load ILBM properly with palette, display splash/title if recoverable

## Analysis Notes
- Binary size 311832 bytes, many small CODE hunks + DATA + BSS + RELOC + SYMBOL + BREAK
- Overlay system explains the unusual header (table size 185)
- frnt5.png = 288x12, inst5.png = 320x55 (5-plane ILBM)
- From community: mostly asm, high-level C for init/music; Bresenham for circles

## Open Questions
- Where is the full splash / title screen graphics stored?
- Exact overlay loading protocol used by the game
- Public disassembly fragments beyond ara's Bresenham snippet

## Bundle History
- fa18-port-001.1-initial-setup-dc0339a.bundle
- (next will be 002.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
