# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] Extracted ADF + assets, project scaffolding, SDL2 skeleton
- [x] Confirmed multi-hunk overlay structure
- [x] String extraction + docs for libraries / missions / credits
- [x] Located primary OpenLibrary (0x4b8) and early init in first CODE hunk
- [x] Identified library-name + path table (~0xd5a0)
- [x] Refined early-init disassembly (Forbid → OpenLibrary → store base at $30)
- [x] High-level flow sketch from strings (title → qualification → missions)
- [x] Improved analyze_binary.py with more LVO recognition

## Next Steps
1. [ ] Overlay / segment reconstruction (or load whole binary at a fixed address for analysis)
2. [ ] Resolve which library is opened at 0x4b8 (name at runtime $50) and subsequent OpenLibrary/OpenDevice calls
3. [ ] Trace control flow from early init to the enlistment / qualification screens
4. [ ] Locate planar bitmaps / copper lists for title & cockpit
5. [ ] Identify main flight loop and 3D transform entry points
6. [ ] Asset pipeline for the existing ILBMs + palette
7. [ ] Begin C++ reimplementation of the high-level state machine (menu / qualification / mission select)

## Analysis Notes
- Game uses absolute low-memory addresses → fixed load address or custom loader
- Only one OpenLibrary in the whole binary; other libraries may be opened via different patterns or after relocation
- ~77 CODE hunks, 7 DATA hunks

## Bundle History
- fa18-port-004.1-early-init-analysis-dc0339a.bundle
- (next: 005.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
