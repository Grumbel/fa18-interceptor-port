# Action Table Create / Clear

## Create function (overlay file 0x4bf14)

```
link a6, #-16
… AllocMem via jsr $30 …
; index at 12(a6)
move.l 12(a6), d0
asl.l  #2, d0              ; ×4
adda.l #0, a0              ; table base = absolute 0
move.l (a0), a1            ; existing slot pointer
move.l #$01660000, 8(a1)
move.l #$003f0000, 12(a1)
move.l #1, 16(a1)
… further init …
```

### Structure fields written by create

| Offset | Value | Notes |
|--------|-------|-------|
| +8 | `$01660000` | |
| +12 | `$003f0000` | |
| +16 | `1` | |
| +32 | `0` (clr) | |

### Table indexing

`index × 4 + 0` → absolute address of the 4-byte slot in the
action table at base **0**.

## Create call sites for slots 35 / 36

These push the index and call **runtime vectors** (not the overlay
directly):

### Site A – file 0x9960 (create slot 35)

```
moveq  #35, d0
move.l d0, -(sp)
pea    $280.L              ; secondary arg
jsr    $1a6.L              ; runtime create vector
```

### Site B – file 0x9976 (create slots 36 and 35)

```
moveq  #36, d0
move.l d0, -(sp)
moveq  #35, d0
move.l d0, -(sp)
jsr    $22c.L              ; runtime create/pair vector
```

`$1a6` and `$22c` are installed at runtime from the overlay (same
pattern as `$12` for draw).  Static image at those addresses is
unrelated code.

## Invoke sites (dispatcher, not create)

File 0xa046 / 0xa05a / 0xa08e / 0xa0a2 push indices 35/36 and
`bsr $9f7c` (the action dispatcher).  These *call* already-created
slots rather than creating them.

## Dispatcher (file 0x9f7c)

```
link a6, #0
move.l 8(a6), d0           ; index
asl.l  #2, d0
adda.l #0, a0              ; table base 0
tst.l  (a0)                ; slot empty?
beq    skip
… call through slot pointer …
```

## Port mapping

```cpp
struct ActionSlot {
  void*  code;           // (slot+0) – function pointer
  uint32_t field_8;      // $01660000 after create
  uint32_t field_c;      // $003f0000
  uint32_t field_10;     // 1
  // …
};

ActionSlot action_table[64];  // base address 0 in original

void action_create(int index) {
  auto& s = action_table[index];
  // AllocMem equivalent …
  s.field_8  = 0x01660000;
  s.field_c  = 0x003f0000;
  s.field_10 = 1;
}

void action_invoke(int index) {
  if (action_table[index].code)
    action_table[index].code();
}
```

## Open

- Exact runtime targets of `$1a6` / `$22c` after overlay load
- Who calls the create sites at 0x9960 / 0x9976 (function containing them)
- Clear function body (companion to create in the same overlay)

## Call chain for slots 35 / 36

```
early init (file ~0x700)
  └─ jsr $9660  (file 0x9960)
       ├─ moveq #35 / pea $280 / jsr $1a6     ; create slot 35
       ├─ moveq #36 / moveq #35 / jsr $22c    ; create slot 36 (paired)
       ├─ move.l $8c, a0                      ; slot 35 pointer (35×4=$8c)
       ├─ move.l $90, a1                      ; slot 36 pointer (36×4=$90)
       ├─ move.l #-1, 16(a0) / 16(a1)
       ├─ move.l #$00b30000, 8(a0)
       ├─ move.l #$00b50000, 8(a1)
       └─ bset #2, $52a
```

`$8c` and `$90` **are** action-table slots 35 and 36 (index × 4).
After create returns, the init code further customises fields +8 and +16
on both slots.

### Runtime vectors used

| Vector | Role |
|--------|------|
| `$1a6` | create single slot (index + secondary arg on stack) |
| `$22c` | create paired slots (two indices on stack) |

Both installed from the overlay at runtime.

## Clear / destroy path

### Wrapper at file 0x4c15a

```
link a6, #-4
move.l 12(a6), d0          ; index
asl.l  #2, d0
adda.l #0, a0              ; table base 0
tst.l  (a0)
beq    empty
move.l 12(a6), -(sp)
bsr    $4c302              ; clear body (may be past this overlay)
…
move.l 8(a6), d0           ; second index?
asl.l  #2, d0
tst.l  (a0)
…
```

### Create wrapper at file 0x4c0ba

```
link a6, #-8
move.l 8(a6), -(sp)
bsr    $4bf14              ; create body
…
move.l 12(a6), -(sp)
move.l -8(a6), -(sp)
bsr    $4bf98              ; secondary init
```

The overlay exports thin wrappers that take indices on the stack and
call the real create/clear bodies.

## Live resolution (in-flight dump)

| Item | Value |
|------|-------|
| Hunk file | `0x4BF14` (header claims 1180 B; only 772 B present in image) |
| Live base | **`$00C4F010`** |
| Create entry | `$00C4F010` (+$00) |
| Gate/check | `$00C4F094` (+$84) – `tst.l $E` |
| Pair wrapper | `$00C4F1B6` (+$1A6) – `bsr` create |

### Create body (+$00) — live helpers

```asm
link    a6, #-16
clr.l   -8(a6)
move.l  #$00010000, -(sp)     ; MEMF_CLEAR | MEMF_PUBLIC
move.l  #$00000110, -(sp)     ; size = 272 bytes
jsr     $00C52710             ; AllocMem (via ExecBase)
addq.l  #8, sp
move.l  d0, -12(a6)
beq.s   fail
addq.l  #4, d0
andi.l  #$FFFFFFFC, d0        ; align
move.l  #$3ED, -(sp)
move.l  8(a6), -(sp)          ; caller arg
jsr     $00C5265C             ; init helper
…
jsr     $00C5268C             ; secondary init
…
jsr     $00C52678             ; library call (jsr -$5A(a6))
move.l  #$110, -(sp)
move.l  -12(a6), -(sp)
jsr     $00C52728             ; FreeMem (temp buffer)
move.l  -8(a6), d0            ; return object ptr
unlk    a6
rts
```

Helpers at `$00C526xx` / `$00C527xx` are thin Amiga library stubs
(ExecBase at `$00C06A9C`/`$00C06AA0`).

### Slot init fields (confirmed live path +$E6)

After allocating the slot record:

| Offset | Value | Notes |
|--------|-------|-------|
| +8 | `$01660000` | |
| +12 | `$003F0000` | |
| +16 | `1` | |
| +32 | `0` (clr.l) | |

### Clear-on-recreate (+$96 path)

```asm
move.l  12(a6), d0          ; index
asl.l   #2, d0
lea     0(a0,d0.l), a0      ; table base 0
tst.l   (a0)
beq.s   empty
move.l  12(a6), -(sp)
bsr.w   clear_body          ; target +$3EE — PAST end of on-disk image
…
; alloc new, init fields, or on failure:
move.l  #$40, d0            ; FreeMem size
move.l  (a0), -(sp)
jsr     FreeMem             ; $00C52728
suba.l  a1, a1
move.l  a1, (a0)            ; null the table slot
```

The full clear body at +$3EE is **beyond the 772 bytes present in the
ADF image** (hunk header claims 1180).  It is either truncated in this
extract or supplied by a subsequent hunk not glued in the file dump.
The FreeMem + null-slot sequence above is the visible portion of clear.

### Status

- [x] Live base `$00C4F010`
- [x] Create body decoded; AllocMem size `$110`, helpers identified
- [x] Slot field init values confirmed
- [x] Clear-on-recreate path; FreeMem + null slot
- [ ] Full clear body (+$3EE) – missing from on-disk image
- [ ] Runtime targets of `$1a6` / `$22c` create vectors

## Runtime vectors `$1a6` / `$22c` resolved

Same relocatable-offset pattern as `$c6` / `$5ee`: the absolute value
is the offset within the action CODE hunk (file `0x4BF14`, live
`$00C4F010`).

| Vector | Offset | Live address | Role |
|--------|--------|--------------|------|
| **`$1a6`** | `$1A6` | **`$00C4F1B6`** | Create wrapper (single slot) |
| **`$22c`** | `$22C` | **`$00C4F23C`** | Create/clear wrapper (gated) |

Chip-RAM at `$1A6` / `$22C` is zero — not low-memory vectors.

### `$1a6` — single-slot create wrapper

```asm
$00C4F1B6  link    a6, #-8
           move.l  8(a6), -(sp)       ; index
           bsr.w   create_body         ; → +$00 ($00C4F010)
           addq.l  #4, sp
           move.l  d0, -8(a6)
           beq.s   fail                ; AllocMem failed
           move.l  12(a6), -(sp)       ; secondary arg
           move.l  -8(a6), -(sp)
           bsr.w   secondary_init      ; → +$84 region / +$B8
           …
```

Call site pattern (primary file `0x9960`):

```
moveq  #35, d0
move.l d0, -(sp)
pea    $280.L
jsr    $1a6.L              ; → $00C4F1B6
```

### `$22c` — gated create/clear

```asm
$00C4F23C  link    a6, #-4
           movem.l a2, -(sp)
           tst.l   $E.L                ; live $00C0631E
           bne.s   proceed
           moveq   #0, d0              ; gated off
           movem.l (sp)+, a2
           unlk    a6
           rts
proceed:
           move.l  12(a6), d0          ; index
           asl.l   #2, d0
           ; table base 0 …
           tst.l   (a0)
           beq.s   empty
           move.l  12(a6), -(sp)
           bsr.w   clear_body
           …
```

Call site pattern (primary file `0x9976`):

```
moveq  #36, d0
move.l d0, -(sp)
moveq  #35, d0
move.l d0, -(sp)
jsr    $22c.L              ; → $00C4F23C
```

## Clear body (live `$00C4F3FE`, offset +`$3EE`)

Previously past the truncated on-disk image; fully present in the
in-flight Slow-RAM dump.

```asm
; $00C4F3FE
link    a6, #0
movem.l d5, -(sp)              ; 48E7 0020
move.l  8(a6), d0              ; index
asl.l   #2, d0
move.l  d0, a0
adda.l  #$00C06460, a0         ; action table base
tst.l   (a0)
beq.s   done                   ; empty slot

; optional sub-block free when bit 31 of object+4 is clear
move.l  d0, a0
adda.l  #$00C06460, a0
move.l  (a0), a1               ; object ptr
move.l  4(a1), d1
andi.l  #$80000000, d1
bne.s   free_main              ; high bit set → skip sub free
move.l  d0, a0
adda.l  #$00C06460, a0
move.l  (a0), a1
move.l  d0, a0
adda.l  #$00C06460, a0
move.l  (a0), a2
move.l  4(a2), -(sp)           ; size from object+4
move.l  (a1), -(sp)            ; pointer from object+0
jsr     $00C52728              ; FreeMem wrapper
addq.l  #8, sp

free_main:
move.l  8(a6), d0
asl.l   #2, d0
move.l  d0, a0
adda.l  #$00C06460, a0
moveq   #64, d0                ; size = 64
move.l  d0, -(sp)
move.l  (a0), -(sp)            ; object pointer
jsr     $00C52728              ; FreeMem
addq.l  #8, sp

move.l  8(a6), d0
asl.l   #2, d0
move.l  d0, a0
adda.l  #$00C06460, a0
clr.l   (a0)                   ; clear table slot

done:
movem.l (sp)+, d5
unlk    a6
rts
```

### Key findings

| Item | Value |
|------|-------|
| Clear entry | **`$00C4F3FE`** |
| Action table base (live) | **`$00C06460`** |
| FreeMem wrapper | `$00C52728` (Exec `- $D2`) |
| AllocMem wrapper | `$00C52710` (Exec `- $C6`) |
| Main free size | 64 bytes |
| Sub-block free | when `object+4` bit 31 clear; size = `object+4`, ptr = `object+0` |

Create allocates 272 bytes (`$110`); clear frees a 64-byte primary object
and optionally a secondary block whose size is stored in `object+4`.
The extra bytes from create are likely the secondary allocation or
alignment padding handled by the init helper (`$00C5265C` / `$3ED` arg).

### Action table snapshot (in-flight)

`$00C06460` holds an array of longword pointers (many non-null).  Slots
that were created during init remain populated mid-flight.

### Status

- [x] Clear body fully recovered from live dump
- [x] Action table live base `$00C06460`
- [x] FreeMem / AllocMem wrappers identified
- [ ] Exact meaning of the 272-byte create vs 64-byte free split
- [ ] Secondary allocation layout (object+0 / +4)

## Create/free size split resolved

| Allocation | Size | Lifetime | Role |
|------------|------|----------|------|
| Create body AllocMem | **272** (`$110`) | Temporary | Workspace for init helpers |
| Create body FreeMem | **272** | end of create | Releases the workspace |
| Outer/wrapper AllocMem | **64** (`$40`) | Permanent | Actual action-slot object |
| Clear FreeMem | **64** | on clear | Matches permanent object |

### Create body sequence (refined)

```asm
; $00C4F010
link    a6, #-16
clr.l   -8(a6)
move.l  #$00010000, -(sp)      ; MEMF_PUBLIC | MEMF_CLEAR
move.l  #$00000110, -(sp)      ; 272 bytes
jsr     $00C52710              ; AllocMem
move.l  d0, -12(a6)
beq     fail
addq.l  #4, d0
andi.l  #$FFFFFFFC, d0         ; align
move.l  #$3ED, -(sp)
move.l  8(a6), -(sp)
move.l  d0, -16(a6)
jsr     $00C5265C              ; init helper A (lib LVO -$54)
; … helpers $00C5268C, $00C52678 …
move.l  $7C(a0), -8(a6)        ; extract result from workspace+$7C

move.l  #$110, -(sp)
move.l  -12(a6), -(sp)
jsr     $00C52728              ; FreeMem the 272-byte workspace

move.l  -8(a6), d0             ; return extracted object/handle
unlk    a6
rts
```

The 272-byte block is **construction scratch**, not the long-lived
action object.  The permanent 64-byte object is allocated by the
create wrapper / pair path (`moveq #64` + AllocMem at `$00C4F0D6`)
and stored into the action table at `$00C06460 + index×4`.

Clear’s main FreeMem of 64 bytes matches that permanent object.
The optional sub-block free (when `object+4` bit 31 clear) handles a
secondary allocation some slots carry.

### Init helpers (library LVOs via base `$00C06AA0`)

| Helper | LVO | Notes |
|--------|-----|-------|
| `$00C5265C` | `-$54` | first init |
| `$00C52678` | `-$5A` | middle |
| `$00C5268C` | `-$66` | last |

Base `$00C06AA0` sits 4 bytes after ExecBase (`$00C06A9C`) — likely a
second library pointer (Intuition/Graphics or game-private).

### Status

- [x] 272 vs 64 split explained (temp workspace vs permanent object)
- [x] Create frees its own 272-byte block before return
- [x] Clear 64-byte free matches wrapper allocation
- [ ] Identify library at `$00C06AA0` and the three LVO functions
- [ ] Layout of the 64-byte action object beyond +0/+4/+8/+12/+16

## Library base `$00C06AA0` = **dos.library**

```asm
; early init $00C0CBB2
lea     $00C06AC0, a1      ; "dos.library"
moveq   #0, d0             ; any version
jsr     -$228(a6)          ; OpenLibrary
move.l  d0, $00C06AA0      ; DosBase
```

Name string at `$00C06AC0`: `"dos.library"`.  Nearby strings also
name `graphics.library`, `intuition.library`, and `df0:pix/…` paths.

`$00C06A9C` holds ExecBase (saved from abs `$4` at `$00C0CA24`).

### Init helpers = dos.library LVOs

| Helper | LVO | dos.library function |
|--------|-----|----------------------|
| `$00C5265C` | `-$54` (−84) | **Lock** |
| `$00C52678` | `-$5A` (−90) | **UnLock** |
| `$00C5268C` | `-$66` (−102) | **Examine** |

Action create therefore performs a **Lock → Examine → UnLock** sequence
on a filesystem object (mission/script/asset path derived from the
caller argument).  The 272-byte workspace holds the FileInfoBlock and
related scratch during Examine.

## 64-byte action object layout (from live table)

Sampled from `$00C06460` slots mid-flight:

| Off | Size | Typical values | Role |
|-----|------|----------------|------|
| +0 | .l | `$000578A8`, `$0000B3E8`, … | code / handler pointer (or hunk offset) |
| +4 | .l | `$00003392`, `$800015CC`, … | flags/size; **bit 31** gates sub-block free |
| +8 | .w/.l | `$0166`, `$0202`, `$0188` | type / mode (create sets `$01660000`) |
| +12 | .w/.l | `$003F`, `$0016`, `$0008` | parameter (create sets `$003F0000`) |
| +16 | .l | `$FFFFFFFF` or `$00000001` | state / refcount (create sets 1; title slots −1) |
| +20..+44 | | 0 | unused / reserved |
| +48 | .l | `$00C4Fxxx` or 0 | secondary pointer (optional sub-object) |
| +52 | .l | size-ish | secondary size when present |

Clear logic matches: if `object+4` bit 31 is clear, FreeMem the
secondary block (`object+0` as ptr in the sub-free path is actually the
code field — the sub-free path uses a different interpretation when
bit 31 is clear, reading size from +4 and pointer from a parallel
object).  Main free always releases the 64-byte block itself.

### Status

- [x] `$00C06AA0` = DosBase (dos.library)
- [x] Init helpers = Lock / UnLock / Examine
- [x] 64-byte object field map from live samples
- [ ] Exact meaning of +0 handler addresses (chip offsets vs CODE)
- [ ] Which filesystem paths create locks for which slots

## Action invoke dispatcher (`$00C16684`)

Does **not** call `object+0` as a function pointer.  Flow:

```asm
; d0 = slot index from 8(a6)
asl.l   #2, d0
lea     $00C06460, a0
adda.l  d0, a0
tst.l   (a0)
beq     empty

move.l  12(a6), -(sp)
bsr     $00C16660              ; side-table clear helper
; …
move.l  (a0), a1               ; action object
move.l  16(a6), d2
asl.l   #1, d2
move.l  d2, $0C(a1)            ; object+12 = arg<<1

; copy object ptr into side table $00C07488[arg]
move.l  12(a6), d1
asl.l   #2, d1
lea     $00C07488, a0
move.l  object, (a0,d1)

move.l  12(a6), -(sp)
jsr     $00C4EBD8              ; central invoke
```

### Central invoke family

| Addr | Behaviour |
|------|-----------|
| `$00C4EBD8` | `object = $00C07478[idx]`; write `object+$14` → `$DFF09C` (INTREQ) |
| `$00C4EBF0` | load object from `$00C07478[idx]`; `jsr` through a computed vector involving object+8/+12 |
| `$00C16660` | clear `$00C07488[idx]`, then tail-call `$00C4EBD8` |

### Side tables

| Base | Role |
|------|------|
| `$00C06460` | primary action object pointers (64-byte objects) |
| `$00C07478` | invoke-index → object (used by `$00C4EBD8`) |
| `$00C07488` | secondary index → object (written by dispatcher) |

### Revised object+0 meaning

`object+0` holds a large numeric field (`$000578A8`, `$0000B3E8`, …) —
likely a **resource ID, chip offset, or packed size**, not a callable
entry point.  Dispatch goes through the side tables + `$00C4EBD8`
family instead.

### Status

- [x] Dispatcher flow decoded (no jsr through +0)
- [x] Side tables `$00C07478` / `$00C07488`
- [x] `$00C4EBD8` writes INTREQ from object+$14
- [ ] Full `$00C4EBF0` vector call semantics
- [ ] Meaning of object+0 numeric field
