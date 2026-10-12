# Audio channel system

## Descriptor table `$00C07478`

Four pointers to per-channel descriptors (AUD0–3):

| Idx | Descriptor | Custom base |
|----:|------------|-------------|
| 0 | `$00C07498` | `$00DFF0A0` AUD0 |
| 1 | `$00C074B4` | `$00DFF0B0` AUD1 |
| 2 | `$00C074D0` | `$00DFF0C0` AUD2 |
| 3 | `$00C074EC` | `$00DFF0D0` AUD3 |

## Descriptor fields (28+ bytes)

| Off | AUD0 value | Role |
|-----|------------|------|
| +00 | `$00DFF0A0` | channel custom-register base |
| +04 | `$00C07488` | related pointer |
| +08 | `$00C07508` | sample/data block |
| +0C | 7 | length / mode |
| +10 | **`$8201`** | DMACON **SET** (bit15\|AUDn) |
| +12 | **`$0001`** | DMACON channel bit (clear path) |
| +14 | **`$8080`** | INTREQ **SET** (bit15\|AUDn int) |
| +16 | **`$0080`** | INTENA/INTREQ channel interrupt bit |
| +18 | `$00C024E8` | further data / sample ptr |

### Channel bit patterns

| Ch | DMACON SET | INTREQ SET | int bit |
|----|------------|------------|---------|
| 0 | `$8201` | `$8080` | `$0080` |
| 1 | `$8202` | `$8100` | `$0100` |
| 2 | `$8204` | `$8200` | `$0200` |
| 3 | `$8208` | `$8400` | `$0400` |

## Thin control API (`$00C4EBxx`)

| Addr | Effect |
|------|--------|
| `$00C4EB60` | `DMACON = desc[+10]` — enable channel DMA |
| `$00C4EB78` | `DMACON = desc[+12]` — channel bit only |
| `$00C4EB90` | `INTENA = desc[+14]`; `INTREQ = desc[+16]` |
| `$00C4EBB8` | `INTENA = desc[+16]`; `INTREQ = desc[+16]` |
| `$00C4EBD8` | `INTREQ = desc[+14]` — request interrupt |
| `$00C4ED00` | complex play: program AUDx regs + enable DMA |

All take a channel index on the stack (`4(sp)`), shift ×4, index
`$00C07478`.

## Complex play `$00C4ED00` (outline)

```asm
movem.l …
move.w  desc[+16], INTREQ     ; clear pending
; load sample pointers from desc graph
; program AUD base registers (len, per, vol via custom base)
move.w  desc[+10], DMACON     ; enable DMA
jsr     $00C4EE08             ; helper
; countdown / wait loop with DMACON clear on finish
move.w  desc[+12], DMACON     ; disable
```

## Relation to actions

Action dispatcher `$00C16684` can call `$00C4EBD8` with its secondary
argument as the channel index — UI/game events trigger sound by
firing the matching audio interrupt.

## Port mapping

```cpp
struct AudioChannel {
  uint16_t dmacon_set;   // +10  $8201/$8202/$8204/$8208
  uint16_t dmacon_bit;   // +12  $0001/$0002/$0004/$0008
  uint16_t intreq_set;   // +14  $8080/$8100/$8200/$8400
  uint16_t int_bit;      // +16  $0080/$0100/$0200/$0400
  // sample pointer, length, period, volume → SDL_mixer / similar
};

void audio_enable(int ch);
void audio_request(int ch);   // mirrors $00C4EBD8
void audio_play(int ch, const int8_t* sample, int len, int period, int vol);
```

## Status

- [x] 4 channel descriptors with AUD custom bases
- [x] DMACON / INTREQ / INTENA bit patterns decoded
- [x] Thin enable / interrupt API at `$00C4EBxx`
- [x] Complex play outline at `$00C4ED00`
- [ ] Sample data format and period/volume source
- [ ] Exec LVO −162/−168 identity (used by sibling helpers)

## Sample block format

Pointed to by `*(desc+4)` → sample list head.  Each node:

| Off | Size | Role |
|-----|------|------|
| +00 | .l | sample data address → AUDxLC |
| +04 | .l | length in **bytes** (asr#1 → AUDxLEN words) |
| +08 | .l | period (low word → AUDxPER, clamped ≤ `$7C`) |
| +0C | .l | volume (low word & `$3F` → AUDxVOL, 0–63) |
| +10 | .l | play countdown (−1 = sticky / infinite) |
| +14 | .l | reload value for +10 when countdown expires |
| +20 | .l | next sample-block pointer (chain) |

## Play routine `$00C4ED00` (full)

```asm
; a1 = channel descriptor
move.w  16(a1), INTREQ          ; clear channel interrupt
move.l  (a1), a0                ; custom base $DFFx0A0
move.l  4(a1), a2               ; sample-list pointer holder
move.l  (a2), a3                ; current sample block

beq     silence                 ; null → stop channel

move.l  (a3), (a0)              ; AUDxLC = sample data
move.l  4(a3), d0
asr.l   #1, d0
move.w  d0, 4(a0)               ; AUDxLEN = bytes/2
move.w  16(a1), DMACON          ; enable DMA (+10)
jsr     $00C4EE08               ; set PER + VOL from sample

; lifecycle
cmpi.l  #-1, 16(a3)
beq     done
subq.l  #1, 16(a3)
bpl     done
move.l  32(a3), (a2)            ; advance to next block
move.l  20(a3), 16(a3)          ; reload countdown
bra     done

silence:
move.w  #0, 8(a0)               ; VOL = 0
move.w  #$7C, 6(a0)             ; PER = default
move.w  18(a1), DMACON          ; disable DMA (+12)
; delay ~$190 iterations
done:
rts
```

## Period/volume helper `$00C4EE08`

```asm
period = sample[+8]           ; clamp to ≤ $7C (124)
AUDxPER = period
volume = sample[+12] & $3F    ; 0–63
if volume > global_limit ($00C07576): volume = limit
AUDxVOL = volume
```

Paula frequency ≈ `3579545 / (period × 2)` Hz (NTSC).  Period 124 ≈ 14.4 kHz.

## Status

- [x] Sample block layout (LC, len, per, vol, countdown, next)
- [x] Play + silence paths decoded
- [x] Period clamp `$7C`, volume 6-bit with global limit
- [ ] Sample data encoding (8-bit signed PCM expected)
- [ ] Who builds/queues sample blocks

## Interrupt-driven dispatch

`$00C4ED00` is **not** called via direct `jsr` from game logic.  It is
installed as a per-channel **handler pointer** in two tables:

### Low-memory handler table (`$00C00322` family)

Triplets (one per channel):

```
.l  channel_descriptor    ; e.g. $00C07498
.l  handler               ; $00C4ED00
.l  sample_list_head      ; e.g. $00C07508
```

### Runtime mirror (`$00C07508` family)

Same triplet pattern at `$00C0751A` / `$1E` / `$34` / `$4A`.

When an audio interrupt fires (INTREQ AUDn bit), the system looks up
the channel’s handler and calls `$00C4ED00` with the descriptor in a1
(or via the descriptor table index).  Game code queues work by writing
a sample-block pointer into the sample-list head; the interrupt handler
picks it up on the next AUD interrupt.

### Queuing model

```
game thread                     audio interrupt
─────────────                   ───────────────
build SampleBlock               $00C4ED00:
block->data = pcm                 read sample list head
block->length_bytes = …           program AUDx + enable DMA
block->period / volume = …        advance countdown / chain
*sample_list_head = block
$00C4EBD8(ch)  → INTREQ         next interrupt → next block
```

### Sample data location

Descriptor-linked pointers such as `$00FC3522` sit outside the Slow RAM
dump (likely Chip RAM samples).  8-bit signed PCM is the Paula native
format; confirmation needs a chip-RAM capture during playback.

## Status

- [x] Handler installed in `$00C00322` + `$00C075xx` tables
- [x] Interrupt-driven, not direct jsr from game code
- [x] Queuing via sample-list head pointer
- [ ] Chip-RAM sample capture for PCM confirmation
- [ ] Exact interrupt server (AddIntServer / custom)

## Exec LVO identity

| Offset | LVO | Function |
|--------|-----|----------|
| −162 (−`$A2`) | **SetIntVector** | `$00C4EBF2` path |
| −168 (−`$A8`) | **AddIntServer** | `$00C4EC52` path |

```asm
; $00C4EC52 — install vertical-blank server
moveq   #5, d0              ; INTB_VERTB
lea     $00C07560, a1       ; Interrupt structure
jsr     -168(ExecBase)      ; AddIntServer
```

### Interrupt structure `$00C07560`

| Field | Value | Notes |
|-------|-------|-------|
| ln_Type | 2 | NT_INTERRUPT |
| ln_Pri | `$88` | priority |
| ln_Name | NULL | |
| is_Data | NULL | |
| is_Code | **`$00C4ED80`** | multi-channel server |

### Multi-channel server `$00C4ED80`

```asm
movem.l …
moveq   #0, d3              ; channel index
lea     $00C07478, a0       ; descriptor table
; for each channel:
move.l  (a0,d3…), …        ; get descriptor
; null-sample check
jsr     $00C4EE3A           ; prep helper
jsr     $00C4EE08           ; set PER/VOL
; advance sample pointers …
```

Runs on VERTB (or as SetIntVector target for AUD interrupts).  Walks
all four channel descriptors each firing and programs Paula from the
queued sample blocks.

## Status

- [x] −162 = SetIntVector, −168 = AddIntServer
- [x] Interrupt struct at `$00C07560` → server `$00C4ED80`
- [x] Server walks AUD0–3 descriptors each tick
