# Mission win / fail logic

How a flight ends: the per-mission checks, the result code, the debrief
messages and what a custom mission can reuse.  Addresses are runtime
addresses (AGENTS.md); record offsets refer to the scenario records at
`$5b47c` (16 x 512 bytes, docs/world_format.md).  "Verified" marks
statements checked in a run (stats overlay, `--stats`, plus `poke`);
everything else is read from the code.

## Mission numbers

`$5ab9e` is the internal mission number.  The F-keys of the mission menu
add 3 (`$2eb66`): F1 = 3 ... F6 = 8.

| `$5ab9e` | Menu | Mission |
|---|---|---|
| 1 | key 1 | free flight |
| 2 / 125 | key 2 / key 3 | carrier practice / formation flight (125 copies the player as lead, slot 4) |
| 3 | F1 | visual confirmation |
| 4 | F2 | emergency defense (Air Force One) |
| 5 | F3 | intercept stolen aircraft |
| 6 | F4 | search and rescue |
| 7 | F5 | intercept incoming cruise missile |
| 8 | F6 | carrier sub mission ("shadow sub") |
| 9 | key 4 | carrier qualification |
| 126 / 127 | | demo flights |

## Variables

| Address | Meaning |
|---|---|
| `$5aa90` | result: 0 running; -1 success, -2 fail, -3 mission 4 own fault, -4 landed after success (pending debrief); 1 / 2 / 4 the same after the debrief, flight continues; 4 is also set directly by a crash (`$26fca`); -16 / -17 end the flight |
| `$5aba1` | enemies spawned (class `$10` records created without spawn flag bit 15, `$3d436`) |
| `$5aba2` | target / spawn cap: the mission table's max count (`$3cf92`) |
| `$5aba3` | enemies destroyed (`$3a3e2`) |
| `$5ab22` | end request: 3 = mission result pending; 2 = ejected pilot down; 1 / -1 = keys (back to menu / restart) |
| `$5ab24` | frames until `$5ab22` is acted on |
| `$5abb8` | debrief delay (frames) |
| `$5ab8f` | aircraft left: 3 in missions, 5 in free flight, 1 in mission 125 |
| `$5ab9f` | enemy level 0-3 (flight log word +2); +1 per completed mission |
| `$5aa84` | 1 = the player can be damaged / crash (0 shows "DESTRUCT IMMUNE") |
| `$5ab40` | 3 = flight starts on the carrier (landing check wants the arrestor wire), 0 = from the base (mission 3) |

## Per-frame check `$385c6`

Called at the end of the scenario update `$36b88` every game frame.  Only
when `$5abc4` bit 6 is set (in-flight systems armed), not in a demo
(`$5aa88` = 0) and `$5ab9e` > 2.  It dispatches on `$5ab9e`:

| Mission | Routine | Success (-1) | Failure |
|---|---|---|---|
| 3 | `$38658` | `$5ab67` < 0: the radar-locked class `$10` target stayed within ±65536 (view offsets, `$460ba`) for 30 checks ("CONFIRMED SIGNATURE") | — |
| 4 | `$38684` | Air Force One (slot 4) on the ground (+3 bit 7), +108 ≤ 800 and slots 8 and 10 gone | AF1 record gone: -3 if the player hit it (+32 bit 7), else -2 |
| 5 | `$387c2` | the stolen F-16s (slots 4 / 6, still flagged +1 bit 3) were approached within a level-dependent box (98304-196608 units per axis), which sends them home (`$388ee`), and `$5aba3` ≥ `$5aba2` | an F-16 reaches cell x ≥ 112: -2 |
| 6 | `$3891c` | the rescue pod (`$5abba`, dropped by the player) landed (+0 bit 7, +76 ≤ 0) within ±65536 units in x and z of the downed pilot (slot 11, `$5ca90`) | pod landed farther away: -2 |
| 7 | `$389a0` | cruise missile (slot 4) destroyed (+32 bit 1) | missile reached its target (+1 bit 7): -2 |
| 9 | `$38ab0` | player on the ground (+2 & `$c080`), +110 = 0 | — |
| 125 | `$38af4` | — | lead aircraft (slot 4) gone: -2 |
| other (8, 10, 126, 127) | `$38b24` | `$5aba3` ≥ `$5aba2` | — |

Missions 4, 5, 7 and the default check freeze the simulation for a few
frames before the result (`$5aaa6` 2-5, nonzero stops the AI); 4 and 7 also
switch to an external view of the object.  Every result goes through
`$38b66`: `$5abb8` = delay, `$5ab22` = 3, `$5ab24` = 4.

After a success (`$5aa90` = -1 or 1) `$38b86` keeps checking `$38baa`:
player on the ground (+33 bit 0, +3 bit 7), caught by the wire on the
carrier (+4 bits 6-7, when `$5ab40` = 3) or on the runway (+4 bit 2), and
stopped (+110 = 0).  Then `$5aa90` = -4.  So a mission only counts once
the player has landed afterwards.

## Kills and the spawn cap

- A hit sets record +0 bit 10.  `$3a35a` (per object) clears it once
  +76 ≤ 0 and counts a kill in `$5aba3` if the kind is class `$10`, not
  kind `$15` (cruise missile), +1 bit 3 clear (spawn flag 15) and +3 bit 7
  clear (not on the ground).  Verified with `poke`.
- Destroyed aircraft stay in their slot as falling wrecks and are freed
  on impact; only then can the slot be spawned again.
- Hits on other classes are cleared without effect: a class `$20` record
  (carrier, sub) is never removed or counted.  Verified for the sub.
- `$3d320` (every spawn: mission list, zones) refuses **any** entry once
  `$5aba1` ≥ `$5aba2`.  `$5aba1` counts every class `$10` spawn without flag
  bit 15, including the cruise missile.
- Zone spawns (`$3d182`, every 16 frames) ignore the level filter of the
  spawn flags and fire again each time the player re-enters the zone,
  provided the slot is free.
- A record created with spawn flag 14 (+5 = 8) and flag 15 (+1 bit 3) turns
  into an enemy when `$381c6` decides so (clears +1 bit 3, `$5aba1`++,
  `$5aba2`++): this is how the stolen F-16s of mission 5 become targets.

## Mission 8 (F6, shadow sub)

The briefing (messages 59, 62) says "SCRAMBLE AND INTERCEPT SHADOW SUB ...
DESTROY SUB WHILE NOW SURFACED ... MULTIPLE BANDITS PROVIDING AIR
SUPPORT".  The code does not check the sub at all:

- **Win**: the default check: `$5aba3` ≥ `$5aba2` = 4, i.e. four enemy
  fighters shot down (verified: kills 4 gives -1 and "WELL DONE!"), then
  land on the carrier for "YOUR LOG NOW REFLECTS THIS COMPLETED MISSION".
- **The sub** (slot 15, template 12, kind `$21`, cell 80,76) is class `$20`:
  hits on it are cleared without effect, and nothing tests kind `$21` or
  slot 15 (verified: a forced hit leaves it in place, kills unchanged).
- **No mission failure**: the default check never sets -2.  The flight
  only ends by losing all 3 aircraft (crash or eject, `$238e0` ->
  "HEY ROOKIE, F/A-18'S DON'T GROW ON TREES") or by the player quitting.
  If a -2 were set (test scenario `end_m6_fail` pokes it), the debrief
  would show message 66, the stolen-aircraft text of mission 5.
- **Enemies**: the spawn list (all levels) is carrier (slot 14), sub,
  a fighter in slot 8 (kind `$12`) and, at enemy level ≥ 2 only, a fighter
  in slot 10.  Zone 3 (cells x 50-76, z 67-76, every mission) adds a
  fighter in slot 12 on each entry.  Zone 4 (cells 75-77 x 75-77, this
  mission only) launches a cruise missile (template 10, kind `$15`,
  slot 4) that flies route 9 towards Moffett Field; nothing checks it in
  this mission (verified: entering zone 4 spawns it, spawned 3/4).
- Consequence: the cruise missile uses one of the 4 spawns but cannot be
  a kill.  At level 0 the fighters come from slot 8 (once) and zone 3
  (repeatedly); if zone 4 was entered before four fighters were spawned,
  at most three fighters can ever appear and the mission can no longer
  be won.  (Derived from the code, not play-tested to the end.)

## Debrief `$231fc`

Runs `$5abb8` frames after a result.  The messages are ids into the page
table at `$54004` (word offsets from `$54004`; a page is `delay, $ff, y`
followed by lines `delay, 3, colour, text, 0`; a page header with delay 0
continues the message).  Per mission 3-8 a 5-word record (offset table
`$540e2`, words from `$54006`) holds the briefing ids and, in word 4, the
success id (`$39510`).

| Result | Mission | Messages |
|---|---|---|
| -1 | 3-8 | record word 4 ("WELL DONE!" for 8), then "RETURN TO BASE" (3) / "RETURN TO CARRIER" |
| -1 | 9 | 74 "LANDING SUCCESSFUL ... QUALIFIED", flight ends (-17) |
| -1 | 125 | 72 "THE LEAD AIRCRAFT HAS BEEN DESTROYED ...", flight ends (-16) |
| -2 | 4 | 36 / 37 (Air Force One destroyed, president on board or not; random) |
| -2 | 5 | 44 "STOLEN AIRCRAFT ARE NOW BEYOND RECOVERY RANGE", flight ends |
| -2 | 6 | 51 "RESCUE POD DEPLOYED TOO FAR FROM DOWNED PILOT" |
| -2 | 7 | 57 "CRUISE MISSILE HAS EXPLODED", then 58 (flight ends) or 92 (random) |
| -2 | other | 66 (stolen-aircraft text) |
| -3 | 4 | 76 "YOU HAVE DESTROYED AIR FORCE ONE", flight ends |
| -4 | any | 84 "YOUR LOG NOW REFLECTS THIS COMPLETED MISSION", log +56 (completed), `$5ab9f` and the mission's level byte (log +18 + mission) +1 (max 3) |

Unless the flight ends, `$22f06` turns -1 / -2 / -4 into 1 / 2 / 4 once the
messages are shown and the player keeps flying.  -16 goes to `$23a54`, -17
to `$23ab0` (both back to the menu).

## Losing the aircraft

- Crash: touchdown at vertical speed ≥ 576 or a collision sets +0 bit 9
  and flight log +70 (crashes); `$22f06` then runs `$238e0`.
- Fatal missile hit: without active flares/chaff and with bit 0 of the
  frame counter clear (`$3a90e`), +32 bit 1 ("TURBINE FAILURE"); else
  "MINOR HYDRAULIC/STRUCTURE DAMAGE".  Missile hits count in log +72.
- An ejected pilot (kind `$30`) on the ground sets `$5ab22` = 2: "YOU HAVE
  SURVIVED YOUR BAILOUT" and the flight ends (`$231a4`).
- `$238e0` decrements `$5ab8f`; at 0: "HEY ROOKIE ..." (98 / 99) and back
  to the menu, else the player is relaunched and the mission continues.

## Custom missions

With world files (docs/world_format.md) and no new code:

- Missions that use the default check (8, 10) are "destroy N enemies":
  set `max=N` on the mission line and provide at least N class `$10`
  enemies (any class `$10` kind except `$15`) without spawn flag 15.
  Everything else in the list, zones included, is decoration.
- Put non-enemy spawns (carrier, ships) before the enemies or keep the
  enemy count ≤ N: once N enemies were spawned every further entry is
  refused.  Zones can feed reinforcements (re-entry respawns), but do
  not let objects that count as spawned but cannot be killed (`$15`) use
  up the cap.
- Missions 3-7, 9 and 125 have checks tied to fixed slots (slot 4 / 6 /
  11, the rescue pod): reusing them means keeping those objects in those
  slots.
- New win/fail conditions (e.g. destroying the sub) need code: a native
  replacement of the dispatcher `$385c6` (or one handler) that sets
  `$5aa90` and calls the `$38b66` tail (`$5abb8`, `$5ab22` = 3,
  `$5ab24` = 4), plus debrief text (message ids, `$231fc`).
