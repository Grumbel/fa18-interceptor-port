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
