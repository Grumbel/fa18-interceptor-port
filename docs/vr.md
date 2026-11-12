# Head-mounted display (`--vr`)

`--vr` is `--renderer new` (docs/new_renderer.md) on a headset through
OpenXR: the 3D view surrounds the pilot and follows the head, the game's
own screen (cockpit, HUD, messages; menus outside the flight) stands where
the original shows it.  The game itself is unchanged, as with every
renderer.  Code: `src/render/vr.{hpp,cpp}` (session, eye framebuffers),
`src/render/vr_math.hpp` (head and eye geometry), `vr_picture` in
`src/main.cpp`.

    fa18 --vr ORIGINAL.adf

Needs a build with the OpenXR loader (CMake finds it: `OpenXR (--vr): ON`;
it is in the Nix dev shell and package) and a running OpenXR runtime with
OpenGL support: SteamVR, Monado, WiVRn.  The window's context must be GLX
(X11) or, for runtimes with `XR_MNDX_egl_enable` (Monado, WiVRn), EGL.
Linux only so far.

Keyboard, joystick and gamepad work as before (the window needs the
focus).  **Ctrl+R** takes the direction the head looks in as straight
ahead; this also happens with the first picture.  The window shows the
middle of the left eye's picture.

## What is drawn

- The head's rotation relative to the seat is applied on top of the game's
  camera (`Shadow::View::head`).  The seat's forward direction is the
  game's view direction, so the view keys and the external views keep
  working and the head looks around from there.
- One display list per picture, for a symmetric frustum around the head
  that holds both eyes' pictures.  The original code culls and sorts with
  the view matrix whose rows 0 and 1 are divided by `Frame::widen` and
  `Frame::heighten`; in the original `|x| <= z` is the tangent
  `scale[2] / scale[0]` (128 / 168, 74.6 degrees across) and `|y| <= z` is
  `scale[2] / scale[1]` (128 / 252).  The renderer draws the list once per
  eye with the eye's own projection (`GlScene::draw_scene_eye`); the
  game's zoom therefore changes nothing in the headset.
- The game's 320x200 screen is a picture in the directions the original's
  projection gives its pixels (`GlScene::draw_screen_eye`): the HUD stays
  on what it marks, the instrument panel is in front of the pilot, and
  below the panel a black console goes on downwards.  For the cockpit and
  the HUD the shadow gets the game's own matrix back, so they are the
  original's whatever the head does.  Outside the flight the screen is
  shown at 80% of that size.
- **Stereo**: each eye sees the scene from its own position (the
  runtime's eye poses, so the headset's IPD), and moving the head moves
  the eyes (relative to where the head was at the last recentring,
  clamped to 0.5 m).  `--vr-mono` puts both eyes at the head's centre.
  Debug menu, Extras, "VR eye distance": the headset's own (the entry
  shows the distance it reports), mono, or a fixed 32 mm to 5 m; larger
  distances make the world look smaller and the depth stronger.
- **Cockpit and HUD apart**: the game's screen is split by the routine
  that drew each pixel.  The HUD's symbols (everything `$47ec0` draws,
  crosshair and waterline marks included) stay at infinity, as a real
  HUD is collimated.  The rest (instrument panel, nose `$455ba`, the HUD's
  frame, drawn by `$48d4a` from `$48e22` on, messages) is a flat picture
  0.7 m ahead of the seat, so it has depth and moves against the world
  when the head moves.  Debug menu, Extras, "VR cockpit": 0.5 to 1.5 m,
  or at infinity (the whole screen as one picture, as before).

  How: the shadow runs the 2D block (`Shadow::Impl::draw_2d`) with the
  bitplanes compared after each routine and at the entries of `$47ec0`'s
  parts (PC hooks); the last routine to change a pixel owns it, in either
  of the two passes (`Frame::overlay_hud`).  The panel's static pixels are
  drawn by no routine in the frame (cockpit).  `FA18_SHADOW_2D_TRACE=1`
  prints the owners per picture, `FA18_SHADOW_2D_DUMP=file.ppm` draws
  them.

## Depth of the captured primitives

The captured view-space vertices are in units that differ per object
(level-of-detail scaling).  The capture records for each primitive its
unit in world units (`Prim::unit`, `display_list.hpp`; 0 = a direction,
drawn at infinity); `kUnitsPerMetre` = 1024 / (5 ft) ≈ 672.  The renderer
subtracts the eye's offset, in that unit, from every vertex before the
projection (`GlScene::scene`, `Target::eye`); the water polygons are
offset in world space.

| Source | Hook (`Capture`) | Unit |
|---|---|---|
| object body (`$31d6c`) | `on_object`, polygons and lines in that frame | 2^(`$5adb0` + 8 - fp-6) |
| distant object (`$32d7e`) | same | 2^(8 + fp-8) |
| ground dots / triangles (`$3c1bc`) | `ground_vertex` | 256 / 2^(fp-22 & 3) |
| gun round, missile (`$41666` from `$414ea` / `$41620`) | `on_transform` | 256 |
| smoke trail (op `$35884`) | `on_transform` (shift in a3) | 2^a3 |
| explosions (`$41866`) | `on_blast_vertex` (`$41ac4`), fill `$44b50` / disc `$43dcc` | 256 · 4^shift |
| sun (`$43306`), other 2D fills | — | infinity |

Checked on the runway: ground vertices of all units put the ground 4.6 to
6.3 m below the eye.  Explosion lines (`$44686`) are not captured.

## Pacing

`xrWaitFrame` paces the pictures.  The game still runs its 60 frames per
second: `present_new` draws pictures until the next game frame is due and
none when the game is behind (a headset or runtime slower than 60 pictures
per second), and every picture shows the moment it is drawn at (`phase`),
interpolated between two game frames as in the window.  Measured with
Monado's simulated headset (20 pictures per second): game at full speed.
One picture costs one run of the shadow (about 2.5 ms) and two draws.

## Testing without a headset

- `ctest`: `vr_view` checks the geometry (axes, recentring, canted eyes,
  eye positions).
- `tests/check_vr.sh ORIGINAL.adf`: `FA18_VR_SIM="yaw,pitch,roll[,fov[,size[,ipd_mm]]]"`
  (degrees: right, up, clockwise; field of view and size of an eye's
  picture; distance of the eyes, default 64, 0 = mono) replaces the
  headset by a fixed head and puts both eyes side by side into the
  window.  Head straight (mono): the original's pixels are where its
  projection puts them; turned and raised head; stereo (1 m apart, to
  make it measurable): sky and panel the same in both eyes, the runway
  further right in the left eye; RAM hashes as the original renderer's.
- The OpenXR session, with Monado's simulated headset and no display
  (stdin must be a pipe):

      sleep 1000 | XRT_COMPOSITOR_NULL=1 monado-service &
      XR_RUNTIME_JSON=.../share/openxr/1/openxr_monado.json fa18 --vr ORIGINAL.adf

  Done on 2026-11-12 with Monado 25.1 for GLX (X11 window) and EGL
  (`SDL_VIDEODRIVER=offscreen`): session, both swapchains, a flight and
  the menus, clean exit.
- SteamVR (2.18, X11, GLX) on 2026-11-12: session, 1764x1764 per eye, 75
  pictures per second on the title screen.  How the flight looks in the
  headset is still to be judged.

## SteamVR and library paths

SteamVR's runtime (`vrclient.so`) needs `libuuid.so.1`, `libGL.so.1` and
`libEGL.so.1` and looks for `libvulkan.so.1` by name; on NixOS none of
them is on a Nix-built program's search path.  Without libuuid no runtime
is found; without the Vulkan loader `xrCreateInstance` fails with
`XR_ERROR_RUNTIME_FAILURE` (-2) and Steam's `logs/xrclient_fa18.txt` says
"failed to find vulkan library".  `vr.cpp` therefore loads the four by
name before it touches OpenXR (a loaded library is found by its name),
and the Nix package has them in its RUNPATH (`postFixup`), the dev shell
in `LD_LIBRARY_PATH`.  `steam-run` is not needed.  That log is the place
to look when SteamVR refuses something.

## Open

- Stereo and head position not yet judged in a real headset.
- A 3D cockpit instead of the flat screen.
- Controllers (OpenXR actions), Windows, OpenGL ES.
- The stats / fps overlay is only in the window.
- With a swapchain format that is not sRGB (none seen) colours are too
  bright.
