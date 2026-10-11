# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] Extracted original ADF + assets
- [x] Project scaffolding (AGENTS, README, CMake, SDL2 skeleton)
- [x] Confirmed multi-hunk **overlay** structure
- [x] Converted available ILBMs (frnt5, inst5)
- [x] Basic CODE/DATA hunk scanner
- [x] String extraction utility + curated interesting-string dump
- [x] Documented libraries, file paths, mission UI strings, credits

## Next Steps
1. [ ] Proper overlay-table parser (reconstruct loadable segments + relocs)
2. [ ] Locate entry point + first AmigaOS calls (OpenLibrary sequence)
3. [ ] Search binary for planar bitmaps / copper lists (title, cockpit, terrain)
4. [ ] Map high-level flow: title → qualification → mission select → flight
5. [ ] Begin isolating 3D math (matrix / projection / clip) routines
6. [ ] Asset pipeline that can display the small ILBMs with correct palette
7. [ ] Study any available JOTD WHDLoad notes / EAB disassembly fragments

## Analysis Notes
- Binary: 311832 B, many small CODE hunks + DATA + RELOC + SYMBOL + BREAK
- Overlay header explains table-size 185
- Credits & mission text fully recoverable from strings
- Most graphics beyond the two tiny ILBMs are embedded or procedural

## Bundle History
- fa18-port-001.1-… (superseded)
- fa18-port-002.1-hunk-scanner-sdl-skeleton-dc0339a.bundle
- (next: 003.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
