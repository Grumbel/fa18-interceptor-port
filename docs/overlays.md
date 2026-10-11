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

## Hunk map (selected)

| Hunk | Type | File payload | Size | Notes |
|------|------|--------------|------|-------|
| 0 | CODE | 0x300 | 41804 | Primary – runtime 0 |
| 35 | CODE | 0x28274 | 2068 | Blitter primitives |
| 36 | CODE | 0x28b4c | 4644 | Blitter / drawing |
| 37 | CODE | 0x2a130 | 2656 | |
| 38 | CODE | 0x2adf8 | 2000 | |
| 39 | CODE | 0x2b7ec | 6480 | |
| 71 | ? | ~0x40f58 | 11604 | **All zeros in file** – BSS-like buffer |
| 72 | ? | ~0x43cb8 | 25564 | **All zeros in file** – BSS-like buffer |

Hunks 71 and 72 are declared in the size table but the corresponding file
regions are entirely zero.  They are almost certainly **runtime-allocated
buffers** (frame buffers, planar bitmaps, or decompression targets) whose
space is reserved in the hunk table but not stored on disk.

## Address table in DATA near 0xc39c

A sequence of longs in the `0x100–0xA000` range starts at file `0xc39c`
(`0x3f2, 0x6a8, 0x6b0, 0x42e, …`).  These look like **offsets into the
primary segment** – possibly a function/entry-point table used by the
overlay or state dispatcher.

## Overlay loader

No standard dos.Read/Seek loop found in the primary segment beyond the
initial Open/Close.  Overlay loading is likely done via:

- A custom track/file loader already in the low-memory vector table, or
- Pre-loaded resident hunks (all 137 non-zero hunks loaded at startup
  by LoadSeg, with “overlays” being just far calls between them).

If LoadSeg loads the entire executable resident, then absolute addresses
in overlay hunks would be assigned by the OS loader and recorded in a
segment list – recoverable dynamically or from the reloc tables.
