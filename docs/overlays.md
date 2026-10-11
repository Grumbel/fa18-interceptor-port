# Overlay / Multi-Hunk Architecture

## Summary

F/A-18 Interceptor is a **multi-hunk overlay executable**, not a single
contiguous CODE image.

## Primary CODE segment

| Item | Value |
|------|-------|
| HUNK_CODE at | file `0x2f8` |
| Payload | file `0x300` |
| Size | 41804 bytes |
| Load model | **runtime address 0** → `file = 0x300 + runtime` |

This segment contains:

- Early init / OpenLibrary
- Picture-wait path (`$01c8`, `$ef4`, `$996`, `$cce`)
- Keyboard test, speed/VBL hooks
- Low-memory vector table setup
- WHDLoad-patched sites

Static analysis with the load model is reliable **inside this segment**.

## Overlay CODE hunks

Dozens of additional `HUNK_CODE` blocks follow, for example:

| File (hunk start) | Payload size | Notes |
|-------------------|--------------|-------|
| 0x0e4b4 | 5800 | |
| 0x0101f0 | 472 | |
| 0x0104e8 | 6420 | |
| … | … | many more |
| 0x02826c | 2068 | blitter-wait code (lea $dff000) |
| 0x028b44 | 4644 | more blitter / drawing |
| 0x02a128 | 2656 | |
| 0x02b7e4 | 6480 | |

These are loaded on demand by the Amiga overlay manager at **runtime
addresses that are not equal to file offsets**.  Absolute `jsr` targets
into overlay space cannot be resolved with the primary load model.

## Blitter / drawing overlays

The dense `lea $dff000` + blitter-wait (`btst #6, 2(an)`) cluster sits
in overlay CODE hunks starting around file **0x2826c–0x2b000**:

- 20+ blitter-wait sites
- Register-based calling convention (almost no `link a6`)
- Performance-oriented drawing primitives

To reverse these, either:

1. Find the overlay table (HUNK_OVERLAY) and its load addresses, or
2. Dynamic trace under an emulator to see where each hunk is placed, or
3. Analyse each overlay hunk in isolation (relative to its own payload
   start) without assuming global addresses.

## Implications for the port

- Logic in the primary segment (menus, mission select, input, resource
  descriptors) can be reimplemented from static RE.
- Drawing / 3D code in overlays needs either overlay-table recovery or
  behavioural RE (what pixels get written, from what inputs).
- The C++ port can stub the blitter primitives and replace them with
  SDL2 drawing without a full overlay reconstruction.
