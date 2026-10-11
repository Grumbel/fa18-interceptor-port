# TODO - FA-18 Interceptor Port

## Current Tip
Initial project setup. Base commit pending.

## Next Steps
1. [ ] Fully catalog all files from original ADF and a clean crack (Unit A preferred for HD install notes)
2. [ ] Parse Hunk headers of main executable; identify code/data/bss segments
3. [ ] Extract IFF pictures (splsh, frnt5, inst5) and convert to modern formats
4. [ ] Dump and analyze text/ strings for localization / UI
5. [ ] Research existing disassemblies (JOTD WHDLoad source if available, EAB threads)
6. [ ] Set up build system (CMake + SDL2) and skeleton that loads assets
7. [ ] Begin static analysis of binary (entry point, library calls to AmigaOS)

## Open Questions
- Is there a public disassembly or partial source anywhere?
- Exact memory map / custom chip usage for 3D rendering
- How is the terrain / Golden Gate etc. stored? Procedural or data?

## Bundle History
None yet.
