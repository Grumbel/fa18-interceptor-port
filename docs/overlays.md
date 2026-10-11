# Overlay / Multi-Hunk Architecture

## HUNK_HEADER

```
file 0x000000  HUNK_HEADER (0x3F3)
  residual   = 0
  tableSize  = 185
  firstHunk  = 0
  lastHunk   = 184
  sizes[185]   (each: upper 2 bits = memory type, lower 30 = size in longs)
file 0x0002f8  end of header / first hunk type word
```

- **137 non-zero hunks**, total payload ≈ **299 KB**
- Memory types: almost all `any`; hunk 62 is `chip`

## Primary segment (hunk 0)

| Item | Value |
|------|-------|
| Size | 10451 longs = **41804 bytes** |
| Payload file offset | `0x300` |
| Load model | runtime address **0** → `file = 0x300 + runtime` |

Contains: early init, picture-wait path, keyboard/VBL hooks, vector setup,
WHDLoad patch sites.  Static RE with the load model applies here.

## Notable large hunks

| Hunk | Bytes | Notes |
|------|-------|-------|
| 0 | 41804 | Primary CODE |
| 2 | 10756 | |
| 5 | 5800 | |
| 8 | 6420 | |
| 17 | 6656 | |
| 28 | 5300 | |
| 36 | 4644 | near blitter cluster |
| 39 | 6480 | |
| 40 | 6124 | |
| 64 | 9256 | |
| 68 | 6224 | |
| **71** | **11604** | large – possible graphics |
| **72** | **25564** | largest – possible graphics / tables |
| 73 | 5800 | |

## Blitter / drawing

`lea $dff000` and blitter-wait loops sit in hunks whose file payloads
fall around **0x2826c–0x2b000** (roughly hunks 35–39).  These use a
register-based calling convention (few `link a6`).

Because overlay hunks are loaded at **runtime addresses ≠ file offsets**,
absolute `jsr` targets into them cannot be resolved with the primary
load model.  Options:

1. Recover the runtime overlay table (custom loader – no standard
   `HUNK_OVERLAY` marker found in the file).
2. Dynamic trace under an emulator.
3. Analyse each overlay hunk relative to its own payload start.

## No standard HUNK_OVERLAY

A scan for type `0x3F5` found nothing.  The game likely uses a **custom
overlay loader** driven by tables inside the primary segment or data
hunks, rather than the official linker overlay scheme.

## Implications for the port

- Menu / mission / resource logic in hunk 0 is statically RE-able.
- Drawing / 3D in overlays can be replaced with SDL2 primitives without
  reconstructing the full overlay graph.
- Asset hunks (71, 72, …) are candidates for extracting title/cockpit
  graphics once their format is identified.
