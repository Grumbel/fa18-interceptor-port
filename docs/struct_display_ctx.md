# Unattributed structures `$00C01550` / `$00C03D46`

Previously mis-identified as `$be`/`$c6` install targets.  The install
vars are numeric scales at `$00C44426`/`$2E`.  These structures remain
real and are referenced from a pointer table around `$00C16E14`.

## Pointer table (in-flight)

| Location | Points to | Notes |
|----------|-----------|-------|
| `$00C16E1E` | **`$00C01550`** | struct_A |
| `$00C16E26` | **`$00C03D46`** | struct_B |
| `$00C16D8A` | `$00C01550` | another ref to A |
| `$00C7FF3C` | `$00C01550` | third ref to A |

## struct_A — `$00C01550` (display / view context)

| Off | Value | Interpretation |
|-----|-------|----------------|
| +00 | 0 | |
| +04 | 0 | |
| +08 | `$00C16D82` | back-pointer into pointer-table region |
| +0C | `$00C055F8` | sub-structure (word stream: `$F180`, `$F182`… — custom-reg style) |
| +10 | `$00C056E2` | related block (odd address) |
| +14 | `$A400` | possible BPLCON0-class constant |
| +1C | `$27`, `$28` | 39, 40 — display window / line pair |
| +28 | `$00C01598` | self+`$48` |
| +2C | `$10` | 16 |
| +30 | `$00C01598` | same |
| +34 | `$08` | |
| +38 | `$08` | |
| +3C | `$39A` | 922 |
| +44 | `$00C05748` | further block |
| +48 | `$00C051D0` | further block |

Hypothesis: **viewport / bitplane context** for the active display mode.
Constants `$A400`, 39/40, 16 fit Amiga display plumbing.  Sub-block at
`$00C055F8` streams values resembling custom-register addresses
(`$F180`…).

## struct_B — `$00C03D46` (secondary context)

| Off | Value | Notes |
|-----|-------|-------|
| +00 | 0 | |
| +04 | `$00C05D10` | pointer |
| +0C | `$002C0081` | |
| +10 | `$40000000` | high bit flag / fixed-point |
| +1C | 1 | |
| +20 | `$8F` | 143 |
| +24 | `$14B1` | 5297 |
| +40.. | more `$00C0xxxx` pointers | |

Less display-like; may be a **scene / object-list context** or mode-
specific scratch.  Shares the “leading zeros + mid-structure pointers”
shape with struct_A but different field population.

## Relation to mode install

Table `$151a` selects numeric scales into `$00C44426`/`$2E`, not these
pointers.  Something else (possibly mode `$220` path, or a one-time
setup) fills the `$00C16E1E`/`$26` pointer slots.  Not yet found as a
`move.l #addr, $…` in the static image (relocatable).

## Status

- [x] Decoupled from `$be`/`$c6` install vars
- [x] Pointer-table refs located
- [x] struct_A: display/viewport hypothesis (BPLCON-class const, DIW-like)
- [x] struct_B: secondary context, under-specified
- [x] Find the writer of `$00C16E22` / `$00C16E26` (AllocMem + field+$22)
- [x] Find the writer of `$00C16E1E` (copy from `$00C16D8A` at `$00C14C74`)
- [x] Confirm copper / bitplane interpretation of `$00C055F8`

## Pointer-table writers (primary)

### AllocMem path → `$00C16E22`

```asm
; live $00C0CDF8 / $00C14922 (primary file 0x6F0 / 0x821A)
pea     <size_or_template>
jsr     $00C52880            ; AllocMem wrapper (ExecBase -$228)
addq.l  #8, sp
move.l  d0, $00C16E22        ; store allocated block pointer
```

`$00C52880` is a thin AllocMem:

```
move.l  ExecBase, a6         ; $00C06A9C
move.l  8(sp), a1            ; attributes
move.l  12(sp), d0           ; size
jsr     -$228(a6)            ; AllocMem
rts
```

In-flight: `$00C16E22` = **`$00C01E1E`** (allocated object).

### Field copy → `$00C16E26`

```asm
; live $00C14938
move.l  $00C16E22, a0
move.l  $22(a0), $00C16E26   ; object field +$22 → table slot
```

So `$00C16E26` (struct_B pointer slot) is filled from **offset +$22 of the
Allocated object**, not by a direct `lea` of `$00C03D46`.  The in-flight
value `$00C03D46` is whatever that field currently holds (or a later
overwrite).

### Summary

| Slot | Writer | Mechanism |
|------|--------|-----------|
| `$00C16E22` | primary `$00C0CDF8`, `$00C14922` | AllocMem result |
| `$00C16E26` | primary `$00C1493E` | copy from alloc’d object `+$22` |
| `$00C16E1E` | (not found as abs store) | may be field of another object or unaligned neighbour |
| `$00C16E2E` | primary `$00C149E4` | `move.l a0, $00C16E2E` after another setup |

The “pointer table” is a set of **globals holding AllocMem’d display /
scene objects**, populated during init/mode change — not the `$be`/`$c6`
install path.

## Copper / bitplane block at `$00C055F8` (confirmed)

The pointer at struct_A+`$0C` targets a **display-register poke / copper-source table**.
Registers are stored as the low 16 bits of their absolute custom addresses
(`$DFFxxx` → `$Fxxx`), a common alternative to the copper’s native `$0xxx` form.

### Header

| Addr | Words | Notes |
|------|-------|-------|
| `$00C055F8` | `$0001 $FFFF` | copper-style wait / terminator flavour |
| `$00C055FC` | `$0000 $0000` | padding / unused |

### Colour palette (COLOR00–COLOR15)

| Reg word | Name | Observed value |
|----------|------|----------------|
| `$F180` | COLOR00 | `$0000` |
| `$F182` | COLOR01 | `$0800` |
| `$F184` | COLOR02 | `$0888` |
| `$F186` | COLOR03 | `$0111` |
| `$F188` | COLOR04 | `$0151` |
| `$F18A` | COLOR05 | `$0447` |
| `$F18C` | COLOR06 | `$0036` |
| `$F18E` | COLOR07 | `$0444` |
| `$F190` | COLOR08 | `$0555` |
| `$F192` | COLOR09 | `$0D92` |
| `$F194` | COLOR10 | `$0666` |
| `$F196` | COLOR11 | `$0030` |
| `$F198` | COLOR12 | `$0333` |
| `$F19A` | COLOR13 | `$0777` |
| `$F19C` | COLOR14 | `$0777` |
| `$F19E` | COLOR15 | `$0333` |

Values are valid 12-bit Amiga colours (`$0RGB`).

### Display window & bitplane control

| Reg word | Name | Value | Meaning |
|----------|------|-------|---------|
| `$F08E` | DIWSTRT | `$0581` | display window start |
| `$F090` | DIWSTOP | `$40C1` | display window stop |
| `$F092` | DDFSTRT | `$0038` | data-fetch start |
| `$F094` | DDFSTOP | `$00D0` | data-fetch stop |
| `$F100` | BPLCON0 | `$0200` / `$4200` | bitplane control (multiple writes) |
| `$F102` | BPLCON1 | (zero-ish) | scroll |
| `$F104` | BPLCON2 | `$0024` | priority |
| `$F108` | BPL1MOD | 0 | modulo |
| `$F10A` | BPL2MOD | 0 | modulo |

### Bitplane pointers (Chip RAM)

| Reg | Name | High / Low | Full address |
|-----|------|------------|--------------|
| `$F0E0`/`$F0E2` | BPL1PT | `$0001` / `$2BC0` | **`$00012BC0`** |
| `$F0E4`/`$F0E6` | BPL2PT | `$0001` / `$4B00` | **`$00014B00`** |
| `$F0E8`/`$F0EA` | BPL3PT | `$0001` / `$6A40` | **`$00016A40`** |
| `$F0EC`/`$F0EE` | BPL4PT | `$0001` / `$8980` | **`$00018980`** |

Four bitplanes in Chip RAM, stride ≈ `$1F40` (8000 bytes) between planes — consistent with a 320×200 (or 320×180) 4-bitplane playfield.

### Layout note

The table is **not** a packed copper list.  Register tags and values are interleaved with zero words (long-alignment or builder scratch).  A copper-list builder (or direct custom-reg writer) walks this block and emits either copper MOVEs or `move.w` to `$DFFxxx`.

### Status

- [x] `$00C055F8` confirmed as copper/bitplane display setup block
- [x] COLOR00–15 palette recovered
- [x] DIW/DDF/BPLCON and four BPLPT addresses recovered
- [ ] Locate the copper-list emitter / custom-reg writer that consumes the block

## Pointer-table topology (complete)

```
$00C16E12  ←  $00C16D74     (copy)
$00C16E16  ←  $00C16D74     (copy)
$00C16E1A  ←  $00C16D8A     (copy)
$00C16E1E  ←  $00C16D8A     (copy)   ★ struct_A slot
$00C16E22  ←  AllocMem result
$00C16E26  ←  (AllocMem object)+$22
$00C16E2E  ←  a0 after setup
```

### Writers for `$00C16E1E` (struct_A pointer)

| Site | Instruction | Role |
|------|-------------|------|
| `$00C14C74` | `move.l $00C16D8A, $00C16E1E` | primary publish |
| `$00C14C38` | `move.l a0, $00C16D8A` then later copy | a0=0 clears intermediate |

`$00C16D8A` itself is filled by:

| Site | Instruction | Role |
|------|-------------|------|
| `$00C14CC4` | `move.l (a0), $00C16D8A` | indexed load: `index×4 + $00C16E1A` |
| `$00C14C38` | `move.l a0, $00C16D8A` | clear path (a0=0) |

Bootstrap: `$00C16E1A` starts as a table of context pointers; an index
selects an entry into `$00C16D8A`, which is then published to
`$00C16E1E`.  In-flight both hold **`$00C01550`** (struct_A).

Related intermediates:

| Slot | In-flight value |
|------|-----------------|
| `$00C16D74` | `$00C01338` |
| `$00C16D8A` | `$00C01550` |
| `$00C16E12` | `$00C05EC0` |
| `$00C16E16` | `$00C01338` |
| `$00C16E1A` | `$00C06A38` (secondary table base) |
| `$00C16E1E` | `$00C01550` |

### Status update

- [x] Writer of `$00C16E1E` found: copy from `$00C16D8A` at `$00C14C74`
- [x] `$00C16D8A` filled by indexed table load at `$00C14CC4`
- [ ] Bootstrap origin of the first `$00C01550` address in the table
      (no absolute `lea #$00C01550` in resident CODE — likely BSS
      fixed address or early AllocMem not yet traced)

## Library bases (display / OS)

| Address | Library | Open site |
|---------|---------|-----------|
| `$00C06AA0` | **dos.library** | `$00C0CBB2` (already known) |
| `$00C16E22` | **graphics.library** (GfxBase) | `$00C0CDEC` |
| `$00C16D60` | **intuition.library** | `$00C0D1BC` |

```asm
; $00C0CDEC
moveq   #29, d0              ; version
pea     graphics.library
jsr     OpenLibrary_wrapper  ; $00C52880
move.l  d0, $00C16E22        ; GfxBase
```

### graphics.library thin wrappers (`$00C52Axx`)

| Wrapper | LVO | Function |
|---------|-----|----------|
| `$00C52AEC` | −210 | MrgCop |
| `$00C52B02` | −216 | MakeVPort |
| `$00C52B18` | −222 | LoadView |
| `$00C52B28` | −228 | WaitBlit |
| `$00C52B94` | −456 | OwnBlitter |
| `$00C52BA4` | −462 | DisownBlitter |

Copper programming is expected to go through **MakeVPort → MrgCop → LoadView** rather than hand-written COP1LC pokes.  Absolute callers of the wrappers were not found in this dump (likely register-indirect).

## Register-programming table at `$00C055F8`

Pointed to by struct_A+`$0C`.  Format (word triples / pairs):

```
.word  $F1xx     ; register = $DFF000 | ($F1xx & $0FFF)
.word  value     ; 12-bit color or control value
.word  $0000     ; pad
```

### Decoded entries (in-flight)

| Reg | Custom | Value | Meaning |
|-----|--------|------:|---------|
| `$F180` | COLOR00 | `$0000` | black |
| `$F182` | COLOR01 | `$0800` | dark red |
| `$F184` | COLOR02 | `$0888` | grey |
| `$F186` | COLOR03 | `$0111` | dark |
| `$F188` | COLOR04 | `$0151` | |
| `$F18A` | COLOR05 | `$0447` | |
| `$F18C` | COLOR06 | `$0036` | |
| `$F18E` | COLOR07 | `$0444` | |
| `$F190` | COLOR08 | `$0555` | |
| `$F192` | COLOR09 | `$0D92` | |
| `$F194` | COLOR10 | `$0666` | |
| `$F196` | COLOR11 | `$0030` | |
| `$F198` | COLOR12 | `$0333` | |
| `$F19A`–`$F19E` | COLOR13–15 | greys | |
| `$F08E` | DIWSTRT | `$0581` | display window start |
| `$F100` | BPLCON0 | `$0200` | |
| `$F104` | BPLCON2 | `$0024` | |
| `$F090` | DIWSTOP | `$40C1` | |
| `$F092` | DDFSTRT | `$0038` | |
| `$F094` | DDFSTOP | `$00D0` | |
| `$F0E0`–`$F0EE` | BPLxPT | bitplane pointers | `$00012BC0` family |

This is a **CPU-side register script**, not a native copper MOVE list
(native copper uses `$0180` not `$F180`).  A loader walks the table and
writes `custom[reg & $0FFF] = value`.

### Status

- [x] GfxBase / IntuitionBase located
- [x] graphics.library LVO wrappers identified
- [x] `$00C055F8` register script decoded (palette + DIW/DDF/BPL)
- [ ] Emitter that walks the script into custom regs
- [ ] View/VPort structures passed to MakeVPort

## Display buffers at `$00C19638` family

Initialized during GfxBase open path (`$00C0CE36`):

```asm
pea     $00C19638            ; buffer descriptor
pea     $00C195F4            ; related
pea     df0:pix/splsh        ; splash IFF path
bsr     load_picture_helper
move.l  d0, $00C19634        ; store result
```

### Buffer descriptor layout (mid-flight)

| Off | `$00C19638` | `$00C19648` | Role |
|-----|-------------|-------------|------|
| +00 | `$00012B98` | flags `$0502` | |
| +04 | 320 | `$0100` | width / flags |
| +06 | 200 | 0 | height |
| +08 | 0 | `$00000A0B` | |
| +0C | — | 320 | width |
| +0E | — | 200 | height |
| +10 | — | **`$00012BC0`** | bitplane 0 |
| +14 | — | `$00014B00` | bitplane 1 |
| +18 | — | `$00016A40` | bitplane 2 |
| +1C | — | `$00018980` | bitplane 3 |
| +20 | — | `$0001A8C0` | bitplane 4 |

5 bitplanes → 32 colors, matching the 16-entry palette in the
register script (plus EHB/extra).  Plane addresses are Chip RAM
(`$0001xxxx`), stride 320×200/8 = 8000 bytes ≈ `$1F40`; spacing
`$1F40` matches (`$12BC0 + $1F40 = $14B00`, etc.).

### Palette mirror at `$00C19600`

12-bit color words identical to the `$00C055F8` register-script
palette (COLOR00–15).  Likely the source fed into LoadRGB4 or the
script emitter.

### Related `$00C16D9A`

Another 320×200 size pair near the struct_A pointer table — candidate
ViewPort.DWidth/DHeight or RasInfo geometry.

### Status

- [x] 5-plane 320×200 display buffer at `$00C19648`
- [x] Splash path loads into `$00C19638` family
- [x] Palette mirror at `$00C19600`
- [ ] Full BitMap/ViewPort/RasInfo struct field map
- [ ] Double-buffer flip path

## Plane-pointer install / buffer select (`$00C15494`)

```asm
tst.b   $00C4450E
beq     skip
move.l  $00C19634, a0       ; current BitMap-like object
move.l  $08(a0), $00C1964C  ; plane 0 → active descriptor
move.l  $0C(a0), $00C19650  ; plane 1
move.l  $10(a0), $00C19654  ; plane 2
move.l  $14(a0), $00C19658  ; plane 3
move.l  $18(a0), $00C1965C  ; plane 4
```

`$00C19634` holds the **active BitMap pointer** (set by splash/picture
load).  Its +8…+18 are the five plane addresses.  Flag `$00C4450E`
gates the copy into the display descriptor at `$00C1964C`.

### BitMap-like object (pointed to by `$00C19634`)

| Off | Role |
|-----|------|
| +08 | plane 0 |
| +0C | plane 1 |
| +10 | plane 2 |
| +14 | plane 3 |
| +18 | plane 4 |

Matches the five Chip-RAM planes observed mid-flight.

### Multiple plane banks in Slow-RAM descriptors

| Base | Plane0 | Notes |
|------|--------|-------|
| `$00C1964C` | `$00012BC0` | primary 320×200×5 |
| `$00C19678` | `$00007FB0` | secondary bank |
| `$00C196A8` | `$0000AB28` | tertiary bank |

Likely double/triple buffering or separate UI vs 3D playfields.

### Status

- [x] Plane-install routine `$00C15494`
- [x] Active BitMap ptr `$00C19634`
- [x] Gate flag `$00C4450E`
- [x] Three plane banks identified
- [ ] Which bank is front vs back during flight
- [ ] Copper/BPL write from active descriptor

## View / ViewPort chain

### View at `$00C16D70`

| Off | Value | Role |
|-----|-------|------|
| +0 | `$00C16D82` | ViewPort* |
| +4 | `$00C01338` | LOFCSN (copper / colormap related) |
| +8 | 0 | SHFCSN |
| +C | `$002C` / `$0081` | DyOffset / DxOffset |

Passed to **MakeVPort** (`$00C52B10` → Gfx LVO −$DE) from `$00C0CEB4` and `$00C14CCE`.

### ViewPort region `$00C16D82`

| Off | Value | Role |
|-----|-------|------|
| +0 | 0 | Next |
| +4 | `$00C05EB8` | ColorMap* (candidate) |
| +8 | `$00C01550` | RasInfo* / extra (struct_A!) |

**InitVPort** (`$00C52B68` → Gfx LVO −$192) is called with `pea $00C16D82` during display init.

### Plane clear via OwnBlitter (`$00C2F228`)

```asm
lea     $00DFF000, a0
; table of plane-ptr addresses: $00C1965C … $00C1964C
jsr     OwnBlitter          ; $00C52B24 → LVO −$E4
move.w  #$09F0, BLTCON0
; … BLTAFWM/ALWM = $FFFF, modules, BLTSIZE from height 200
; walks plane pointer table and blitter-clears each plane
```

Not the copper emitter — it is a **full-frame bitplane clear** using the active display descriptor.

### Status

- [x] View `$00C16D70` → ViewPort `$00C16D82`
- [x] InitVPort + MakeVPort call sites
- [x] Plane-clear via OwnBlitter at `$00C2F228`
- [ ] ColorMap object at `$00C05EB8` contents
- [ ] Copper list pointers (LOFCSN) confirmed
- [ ] True copper/BPL emitter still open
