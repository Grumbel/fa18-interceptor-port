# FA-18 Interceptor Port

## Goal
Reverse engineer the Amiga game F/A-18 Interceptor (1988, Electronic Arts / Intellisoft), decompile/port it to modern C++ with SDL2 for Linux (and other platforms).

## Source Material
- Original ADF from TOSEC: FA-18 Interceptor (1988)(Electronic Arts).adf
- Main executable: `F-18 Interceptor` (Amiga Hunk format, ~312 KB)
- Assets: pix/ (IFF images), text/ (localized strings?), config

## Known Facts
- Developer: Bob Dinnerman (Intellisoft), graphics Greg Johnson, music Dave Warhol
- Mostly 68000 assembly; some high-level in C
- 3D polygon flight sim set in SF Bay Area
- Codewheel protection (bypassed in cracks)
- WHDLoad slave exists by JOTD with some disassembly notes
- Someone on EAB (ara) was decompiling parts (Bresenham circle etc.)

## Approach
1. Extract and document all assets from ADF
2. Parse Amiga Hunk executable structure
3. Disassemble with tools (vasm/vda, rexx, or custom; or Ghidra later)
4. Identify subsystems: graphics (custom chipset blitter/copper?), 3D engine, physics, UI, sound, missions
5. Reimplement in C++/SDL2 incrementally, starting with asset loading and 2D UI, then 3D renderer
6. Use RE knowledge from community where available

## License
GPLv3+ for new code. Original game is abandonware but respect IP; this is a clean-room / RE educational port.

## Continuity
See TODO.md for current status. Deliver as git bundles only.
