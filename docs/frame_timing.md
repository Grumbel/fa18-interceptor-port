# Frame timing, text pacing and frame rate

How the game paces itself, why texts run too fast in the port, and what a
frame rate above 15 fps would take.  Addresses are runtime addresses
(LOAD_BASE $20000).

## The game frame ($2112c)

The main loop calls `game_frame` ($2112c, native in flow.cpp) once per
pass.  It has two paths, selected by `in_flight` ($5aa8d):

- **In flight** (3D view): input, state machine, simulation, drawing, then
  the frame limiter `$3987e` and the 8-frame window update `$399f6`.
- **Menus and other text screens** (`in_flight == 0`): only input, the
  state machine, sound update `$24aa8`, `FreeSprite`, `$3fba6`.  **No frame
  limiter**: the loop runs as fast as the CPU allows.

Both paths end with the text stream `$478f2` (menu texts, briefings, the
cockpit message line).  It advances by one step per call.

## Frame limiter ($3998c)

`$3987e` reads the system time (`$28e5c`: timer.device TR_GETSYSTIME, copied
to `$5adea` seconds / `$5adee` micros) and then busy-waits in `$3998c`:

    ms = time since $5adf2/$5adf6            ; start of the 8-frame window
    $5ae02 = ms
    loop while ms / ((frame_counter & 7) + 1) < table_3959a[$5abb6]

`table_3959a` holds 1000/n ms (−1, 1000, 500, 333, 250, 200, 167, 143, 125,
111, 100, 91, ..., 67); `$5abb6` is set to 15 (`$2d2f6`).  So frame k of
an 8-frame window cannot finish before (k+1)×67 ms: a 15 fps cap with
catch-up inside the window.  `$399f6` runs every 8th frame
(`frame_counter & 7 == 7`), stores 800000/ms (fps×100) in `$5adde` (read
only at `$46782`) and starts the next window.

### What uses real time

Almost nothing.  The simulation advances a **fixed step per frame**: no
routine scales by the frame duration (`$5ae02` and `$5adde` have no other
readers).  Physics, AI, weapons and the frame-counted timers
(`frame_counter` $5abd2, countdowns like the message hold `$5aa3c`) all
step once per frame.  Below the cap the game simply runs slower (a stock
A500 manages 6–8 fps in flight, so the original runs at roughly half
speed in flight).  The wall-clock exceptions:

- `$39910`: once per second (when `$5adea` changes) the countdowns
  `$5ab7c`, `$5ab7d`, `$5ab7e`, `$5ab89`, `$5ab82` and the `$5ab81` ramp.
- `$398b2`: ms accumulator `$5ae08`; sets bit 6 of `$5abc6` every 10 s.
- flight time for the log, pauses (`$5adea` vs `$5ac0c`, `$5abfc`,
  `$5ac04`, run_game_state in flow.cpp).

## Why texts run too fast in the port

Measured in the mission6_1 scenario, calls of `$478f2`:

| path | stock A500 (`--speed 1 --native off`) | default (`--speed 4`, native) |
|---|---|---|
| menus | ~3 per vblank (180/s) | ~48 per vblank |
| in flight | 5–10/s (one per frame) | 15/s |

The menu path has no limiter, so its pace depends on emulated CPU speed,
and native routines take no emulated cycles at all.  In flight the
stream is paced by the frame rate, which is twice the original.

## Text pacing (`--text-pace`)

Remaster option, off by default (debug menu "Text pacing", script
`textpace on|off`), in `game_frame` (flow.cpp):

- menu path: at most 3 passes per vertical blank (the stock A500 rate).
  Counted per vblank because emulated time does not advance while native
  code runs; the third pass idles until the next vblank.
- in flight: `$478f2` is called only if at least 100 ms have passed since
  the last call (every 2nd frame at 15 fps, every frame at 10 fps and below,
  i.e. the A500's rate).  The state machine waits for messages to finish
  (`$5aad8` < 0); that wait just gets longer.

## Higher frame rates

Raising the cap (`$5abb6` = 30, or skipping the wait in `$3998c`) would
not give smoother 15 fps-speed gameplay: since everything steps per
frame, the whole game would run 2× (or more) faster, except for the few
wall-clock timers above.  A dt-based simulation would mean rewriting
every per-frame integration in the 3D engine and AI (hunks 4–77), with no
reference to check against.  Not realistic.

What could work is **render-side interpolation**: keep the simulation at
15 Hz and draw 60 Hz frames in between with the `--gl` renderer:

- **Camera reprojection** (most of the benefit): re-render the last
  frame's world-space scene with a camera interpolated between the last
  two simulation frames.  Interpolating costs one simulation frame of
  latency (67 ms); extrapolating avoids it but overshoots in turns.  Camera
  rotation is what makes flight look choppy, so this alone helps a lot.
- **Object motion**: the renderer gets primitives, not object ids, so
  moving objects (other aircraft, missiles) would need to be matched
  between frames, e.g. through the object list (docs/object_list.md),
  and their primitives tagged with the object.  Without that they keep
  stepping at 15 Hz on top of a smooth camera.
- **HUD and cockpit** stay 15 Hz (they are the original 2D drawing).
- The game state stays exactly as with the original, so the gate is
  unaffected; the work is in the GL renderer and the hi-res composition.

Estimated effort: medium to large; not started.
