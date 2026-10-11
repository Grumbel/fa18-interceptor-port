# FA-18 Interceptor – Binary Structure Notes

## File
`assets/F-18_Interceptor_amiga` (311 832 bytes)

## Format
Amiga Hunk executable with **overlays**.

- Starts with standard `HUNK_HEADER` (0x000003F3)
- Header table size field = 185 (indicates large overlay table)
- Hundreds of subsequent hunks: many small `HUNK_CODE`, `HUNK_DATA`,
  `HUNK_RELOC32/16/8`, `HUNK_SYMBOL`, `HUNK_END`, and `HUNK_BREAK`
- This layout is typical of Amiga programs that load code segments on demand
  to stay within the limited Chip/Fast RAM of an A500-era machine.

## Libraries used (from strings)
- dos.library
- graphics.library
- intuition.library
- audio.device
- input.device / keyboard.device / gameport.device / timer.device

## File references (from strings)
- `df0:pix/splsh`, `df0:pix/inst5`, `df0:pix/frnt5`
- `df0:config`
- Many `df0:text/text*` files (some only present in cracked HD installs)

## Notable UI / mission strings (excerpt)
- "F/A-18 INTERCEPTOR"
- "YOU ARE NOW OFFICIALLY ENLISTED"
- Qualification / selectable missions (F1–F6)
- Alerts: IR MISSILE, RH MISSILE, CRUISE MISSILE
- Carrier landing / launch messages
- Credits: Bob Dinnerman, Dave Warhol, Moses Ma, Electronic Arts San Mateo

## Graphics assets on disk
- `pix/frnt5`, `pix/inst5` – small 5-plane ILBMs (converted to PNG)
- `pix/splsh` – empty (0 bytes) on original ADF; splash graphics likely
  reside inside the executable or are generated

## Next RE targets
1. Parse the overlay table so individual segments can be loaded/relocated
2. Locate the true entry point and initial OpenLibrary calls
3. Identify planar bitmap / copper-list data for title & cockpit
4. Map the 3D transformation / projection routines

## Entry-point / early initialisation notes (2026-10-11)

First large `HUNK_CODE` starts at file offset **0x000300** (payload).

Observed early sequence (crude disasm):

```
0x300: move.l d0, $40          ; or absolute store
0x306: move.l a0, $44
0x30c: move.l a7, d0
0x30e: movem.l …,-(a7)         ; save regs
… get ExecBase …
0x48e: move.l 4.w, a6
0x492: jsr -108(a6)            ; possibly Forbid / other
0x4b0: lea $50, a1             ; library name pointer?
0x4b8: jsr -552(a6)            ; OpenLibrary
0x4bc: move.l d0, $30          ; store library base
```

Only **one** clear `jsr -552(a6)` (OpenLibrary) was found in the whole binary (at 0x4b8).  
Several `move.l 4.w, a6` (ExecBase) sites exist.

Library name strings are clustered around **0xd5a0** together with the `df0:pix/…` and `df0:text/…` paths.  
This block is almost certainly referenced by the early OpenLibrary / file-open code.

## Next concrete RE tasks

1. Full disassembly of the 0x300–0x600 region (with a proper 68k tool or Ghidra later)
2. Resolve what library is opened at 0x4b8 and what the subsequent bases are
3. Trace the path from early init to the title / qualification screens using the known string addresses
4. Locate planar graphics data (look for copper-list patterns or large zero-padded bitplane blocks)

## Early init – refined (0x300 region)

```
0x300  move.l d0, $40
0x306  move.l a0, $44
0x30e  movem.l …,-(sp)
…      move.l 4, a0 / a6 setup
0x32e  jsr FindTask
…      more setup, absolute stores
0x48e  move.l 4.w, a6          ; ExecBase
0x492  jsr Forbid
0x4b0  lea $50, a1             ; expected library name pointer
0x4b6  moveq #0, d0            ; version = any
0x4b8  jsr OpenLibrary
0x4bc  move.l d0, $30          ; store base
0x4c4  rts
```

The absolute addresses ($30, $40, $44, $50, …) indicate the game expects a
fixed load address or performs its own relocation / low-memory setup after
the overlay loader runs. This is common for large Amiga titles of the era.

## Calling convention / absolute addressing

The binary contains **~981 `jsr abs.l`** instructions.  
The most frequent targets are low absolute addresses:

| Address   | Approx. calls | Notes                          |
|-----------|---------------|--------------------------------|
| $00000000 | 69            | null / special                  |
| $00000012 | 46            |                                |
| $00000030 | 31            | library base stored here after OpenLibrary |
| $00000164 | 35            |                                |
| $00000c58 | 25            |                                |
| …         |               | many more low addresses        |

This indicates the game was built for a **fixed load address** (or performs its own relocation into a private low-memory layout).  
Most inter-routine calls are absolute rather than PC-relative or via a relocatable jump table.  

Consequence for RE / porting:
- When analysing, treat the binary as loaded at a known base (or ignore the absolute values and focus on control flow + data).
- A full overlay/reloc parser is less critical for understanding logic than for producing a byte-exact reassembly.

## String cross-references

File offsets of key strings (e.g. `"YOU ARE NOW OFFICIALLY ENLISTED"` @ 0x3a68b)
are **not** the runtime addresses. Because the binary uses absolute addressing
with a fixed (or privately relocated) load base, a simple search for the
file-offset value as an immediate finds nothing.

To locate code that uses a particular string we will need either:

1. Knowledge of the runtime load address, or
2. Pattern matching on the string content itself after it has been copied /
   the surrounding code that builds a pointer to it, or
3. Dynamic tracing under an emulator (WinUAE / FS-UAE with a debugger).

This is a normal limitation of fixed-address Amiga executables.

## WHDLoad slave notes (JOTD)

Source available at https://github.com/jotd666/whdload_slaves (F/F-18Interceptor).

Key observations from `F-18InterceptorHD.s`:

- Protection removal comment: offset `1ce70` : `13fc00ff -> 13fc0001`
- After `LoadSeg` the slave patches the segment list with:

```
PL_PS   $8E2, _emulate_empty_loop
PL_B    $19737, 1          ; protection-passed flag
PL_B    $3a964, 1          ; another protection flag
PL_NOP  $01a78, 6
PL_L    $fd7c, $4EB80100
PL_PSS  $11468, _fix_af_2, 2
PL_P    $451c8, sound_delay
PL_PSS  $01590, keyboard_test, 2
PL_PS   $006b4, wait_pic   (BUTTONWAIT)
PL_PS   $08286 / $092e6    speed regulation / VBL hooks
```

These offsets are relative to the loaded segment base (post-LoadSeg).  
They give us concrete runtime addresses once the load base of the first
code segment is known, and they mark important control points
(protection, keyboard, speed regulation, picture wait).

The slave also forces a protection-passed flag and installs a small
empty-loop emulator and keyboard/VBL hooks.

## Protection offset confirmation

JOTD comment: `offset 1ce70 : 13fc00ff -> 13fc0001`

File bytes at **0x1ce70**:

```
13 fc 00 ff 00 00 02 7c 4e 75 4e 75 …
```

Exactly matches the original `move.b #$ff, …` that the slave patches to `#$01`.  
This shows that (at least for this region) the PL_ offsets in the WHDLoad slave correspond directly to **file offsets** in our extracted executable (or the first code segment is loaded at a base that makes the numbers coincide).  
Further PL_ sites can therefore be examined at the same numeric offsets in the binary.

## Mapped WHDLoad sites (see also docs/whdload_sites.md)

Confirmed that PL_ offsets == file offsets for the main executable body.
Key anchors now available for static analysis:

- Protection `move.b #$ff,$27c` @ 0x1ce70
- Empty-loop timing sequence @ 0x08e2
- Keyboard test @ 0x1590
- Speed/VBL hooks @ 0x8286 / 0x92e6
- Picture-wait @ 0x06b4

## Graphics / blitter region

Dense cluster of `lea $dff000, …` and blitter-wait loops at
**0x288c8 – 0x2aa30**.  See `docs/graphics_blitter.md`.

## Jump-table architecture

~197 unique `jsr abs.l` targets below $1000.  The game installs a
low-memory vector table during startup; most inter-routine calls go
through it.  See `docs/jump_table.md`.

## Load model

First CODE hunk payload (file 0x300) ↔ runtime address 0.
`file_offset = 0x300 + runtime_addr` for addresses in that segment.
See `docs/load_model.md`.
