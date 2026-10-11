# TODO - FA-18 Interceptor Port

## Current Tip
(see latest commit)

## Completed
- [x] Extracted original ADF + assets
- [x] Project scaffolding (CMake, SDL2 skeleton)
- [x] Confirmed multi-hunk overlay structure
- [x] Converted available ILBMs
- [x] String extraction + curated dumps
- [x] Documented libraries, paths, credits, mission text
- [x] Located primary OpenLibrary site (0x4b8) and early init in first CODE hunk (0x300)
- [x] Identified library-name + path data block at ~0xd5a0
- [x] Added tools/analyze_binary.py for quick static checks

## Next Steps
1. [ ] Proper overlay-table / segment reconstruction
2. [ ] Full disassembly of early init (0x300–0x600) – resolve which library is opened and subsequent bases
3. [ ] Trace from early init to title/qualification using known string addresses
4. [ ] Search for planar bitmaps / copper lists
5. [ ] High-level flow map (title → qualification → mission select → flight loop)
6. [ ] Isolate 3D math / projection routines
7. [ ] Asset pipeline for ILBMs with correct palette

## Analysis Notes
- First large CODE hunk @ file 0x300 appears to be the main program
- Only one clear OpenLibrary call found; library bases stored at low absolute addresses ($30 etc.)
- Library names + df0: paths clustered together – classic Amiga resource table

## Bundle History
- …-002.1-… (superseded)
- fa18-port-003.1-strings-and-docs-dc0339a.bundle
- (next: 004.x)

## Author
Ingo Ruhnke <grumbel@gmail.com>
Co-authored-by: Grok <grok@x.ai>
