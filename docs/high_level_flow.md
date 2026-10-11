# FA-18 Interceptor – High-level Flow (from strings + early analysis)

## Startup (file offset ~0x300)

1. Custom entry / overlay loader hands control to first large CODE hunk.
2. Early absolute stores to low memory ($30, $40, $44, …).
3. `move.l 4.w, a6` → ExecBase
4. `Forbid`
5. `OpenLibrary` (single call at 0x4b8) – library name expected at absolute $50;
   result stored at $30.  
   (Library name table + path strings live at file ~0xd5a0; runtime copy or
   fixed load address is likely.)
6. Subsequent opens of graphics / intuition / devices / files under `df0:`.

## Title / Enlistment

- Splash / title graphics (location still unknown – not the empty `pix/splsh`)
- String: `"YOU ARE NOW OFFICIALLY ENLISTED"`
- Credits: Bob Dinnerman, Dave Warhol, Moses Ma, Electronic Arts

## Qualification

- `"QUALIFICATION: REQUIRED FOR MISSIONS"`
- Basic flight maneuvers (Part 1 / Part 2)
- Carrier landing practice
- Must pass to unlock combat missions

## Mission Select

```
SELECTABLE MISSIONS
 F1 - VISUAL CONFIRMATION MISSION
 F3 - INTERCEPT STOLEN AIRCRAFT
 F5 - INTERCEPT INCOMING CRUISE MISSILE
 F6 - CARRIER SUB MISSION
```
(Also F2/F4 exist in some versions; exact mapping TBD)

## In-flight / Alerts

- `ALERT: IR MISSILE` / `RH MISSILE` / `CRUISE MISSILE`
- Fuel warnings, stall, low altitude, collision proximity
- Carrier launch / landing messages
- Mission success / failure text (Air Force One, nuclear destruction, etc.)

## Flight Log / Stats

- Missions started / completed
- Hits from missiles
- Update log to disk option

## Known file resources

- `df0:pix/frnt5`, `df0:pix/inst5` (small ILBMs – instruments / HUD)
- `df0:config`
- Multiple `df0:text/text*` (localised or mission text blocks)

## Open questions for flow

- Exact order of screens after OpenLibrary
- Where the full title/splash bitmap lives
- How overlays are loaded for different mission / view modes
- Location of the main flight loop and 3D renderer entry

## Implementation status (C++ skeleton)

A minimal `Game` state machine now exists (`src/game_state.*`).

- States mirror the flow above.
- SPACE / ENTER advances, BACKSPACE goes back, ESC quits.
- Window background colour changes with the current state (visual feedback).
- Mission list is populated from the recovered F-key strings.

Next implementation steps will replace the colour-only screen with real
asset rendering (ILBMs, later title/cockpit bitmaps) and will drive the
state transitions from the actual RE’d logic instead of manual keys.

## Title-transition path (from keyboard)

```
key pressed ($155) + input enabled ($206)
  → mode handler $64c
    → $9d7a (if flag $52b bit 7)
      → action table[35] and table[36] via 0x9f7c
```

Picture-wait (`$01c8`) is a sibling path in the same phase, entered
from file 0x9d84 after `$84` setup – not directly from `$64c`.
