# Live Symbol Map (in-flight dump)

Derived from `dumps/inflight/fa18_inflight_code.bin` taken while airborne
with rockets firing (FS-UAE, Slow RAM at `$00C00000`).

## Memory layout (from `dm`)

```
$00000000  512K Chip memory
$00C00000  512K Slow memory   ← all game CODE + BSS/DATA
$00C80000  Custom chipset
```

Primary CODE loads at **`$00C0CA08`** (dump offset `0xCA08` when the
dump starts at `$C00000`).

## Relocation rule

On-disk absolute addresses of the form `$0000xxxx` (and many immediate
longs used as addresses) are fixed up at load time into the
`$00C0xxxx`–`$00C5xxxx` range.  Chip low memory is **not** where game
state lives.

```
file_offset = 0x300 + runtime_r          (primary CODE, unrelocated view)
live_addr   = $00C0CA08 + runtime_r      (this session)
dump_offset = 0xCA08 + runtime_r         (inflight code dump @ $C00000)
```

## Canonical live addresses

| Orig (static) | Live (this session) | Role | In-flight value |
|---------------|---------------------|------|-----------------|
| `$000C` | **`$00C16D6C`** | current object pointer | `$00C44E2A` |
| `$0012` | **`$00C062C2`** | draw-mode vector | `4E75` (RTS stub) |
| `$00BE` | **`$00C16E1E`** | sibling vector | `$00C01550` |
| `$00C6` | **`$00C16E26`** | post-transform / draw vector | `$00C03D46` |
| `$036E` | **`$00C446D6`** | DrawMode word | `$0000` |
| `$04B0` | **`$00C44818`** | input axis 0 | `$0000` |
| `$04B2` | **`$00C4481A`** | input axis 1 | `$0000` |
| `$052B` | **`$00C44893`** | shared keyboard/picwait flag | `$F7` |
| `$0642` | **`$00C449AA`** | object pool base | (pool start) |
| `$0B68` | **`$00C44ED0`** | world X | `$102F9D70` |
| `$0B6C` | **`$00C44ED4`** | world Y | `$000D7838` |
| `$0B70` | **`$00C44ED8`** | world Z | `$10E91BA8` |
| `$0B8F` | **`$00C44EF7`** | active object count (byte) | `$00` |
| `$0BB4` | **`$00C44F1C`** | phase timer | `$01F4` |
| `$0BD1` | **`$00C44F39`** | table A | `$53140038` |
| `$0BE6` | **`$00C44F4E`** | 3×3 matrix (Q2.14) | live |
| `$0BF6` | **`$00C44F5E`** | matrix end | — |
| `$05EE` | *(overlay only)* | geometry hand-off | not in primary |

### Matrix at `$00C44F4E` (in-flight)

```
row0:  14437   1574  -7583
row1:   -311  16147   2761
row2:   7739  -2290  14259
```
(Q2.14; 16384 ≈ 1.0)

### Object pool

- Base: `$00C449AA`
- Stride: 64 bytes
- Max: 20
- Count at dump time: 0 (end-of-frame or rockets already cleared)
- Current object (`$c`): `$00C44E2A` (near the pool)

### Vector notes

- `$12` holds an **RTS stub** (`4E75`) in this snapshot — the real
  draw-mode bodies are installed only while a mode is active, or live
  in an overlay not resident at dump time.
- **`$c6` has two meanings:**
  1. *Static relocatable reference* — `jsr $c6.L` in primary resolves
     at load time to the object-worker function at **`$00C25206`**
     (offset `$C6` inside the CODE hunk at file `0x1EA04`).
     See [func_c6.md](func_c6.md).
  2. *Vector slot* at live `$00C16E26` (written by overlay install from
     table `$151a`).  Held `$00C03D46` in this dump — separate mechanism.
- `$be` vector slot at `$00C16E1E` → `$00C01550` (sibling of the
  vector-slot `$c6`).

## How the map was built

1. Locate primary CODE in the dump by sequence match
   (`dump_start = 0xCA08`).
2. Scan every 2-byte-aligned longword: on-disk `0000xxxx` that becomes
   `00Cxxxxx` in the dump → fixup pair.
3. For critical symbols, confirm with the exact instruction site
   (e.g. `move.l a0,$c.l` at file+`$732C` → `$00C16D6C`).
4. Immediate longs used as addresses (`adda.l #$642,a0`) are likewise
   fixed up and recorded.

2745 fixups, 558 unique original addresses were recovered from the
primary CODE hunk alone.

## FS-UAE dump commands (for future sessions)

```
console_debugger = 1          # in .fs-uae config; run from terminal
# F12+D to enter debugger, then:
S fa18_inflight_slow.bin C00000 80000
S fa18_inflight_chip.bin 0 80000
g
```

Slow RAM alone is sufficient for all game state and CODE.

## Pre-flight vs in-flight

| Item | Pre-flight | In-flight |
|------|------------|-----------|
| Primary dump offset | `0xA08` | `0xCA08` |
| Primary live base | same fixup targets | `$00C0CA08` |
| World X/Y/Z | zero | live non-zero |
| Matrix | zero | live Q2.14 |
| `$c6` vector | zero / unset | `$00C03D46` |
| Object count | 0 | 0 |

Both dumps share the same relocation bases (`$00C06A70` self-refs,
`$00C0CA08` primary).  The pre-flight `fa18_code.bin` simply started
at a different file offset relative to `$C00000`.
