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
- **Everything is at infinity**: both eyes see the same, and moving the
  head does nothing.  For what is seen from a flying aircraft that is
  right, and a real HUD is collimated as well; on the ground near objects
  have no depth, and the cockpit is flat.

## Pacing

`xrWaitFrame` paces the pictures.  The game still runs its 60 frames per
second: `present_new` draws pictures until the next game frame is due and
none when the game is behind (a headset or runtime slower than 60 pictures
per second), and every picture shows the moment it is drawn at (`phase`),
interpolated between two game frames as in the window.  Measured with
Monado's simulated headset (20 pictures per second): game at full speed.
One picture costs one run of the shadow (about 2.5 ms) and two draws.

## Testing without a headset

- `ctest`: `vr_view` checks the geometry (axes, recentring, canted eyes).
- `tests/check_vr.sh ORIGINAL.adf`: `FA18_VR_SIM="yaw,pitch,roll[,fov[,size]]"`
  (degrees: right, up, clockwise; field of view and size of an eye's
  picture) replaces the headset by a fixed head and puts both eyes side by
  side into the window.  Head straight: the original's pixels are where
  its projection puts them; turned and raised head; RAM hashes as the
  original renderer's.
- The OpenXR session, with Monado's simulated headset and no display
  (stdin must be a pipe):

      sleep 1000 | XRT_COMPOSITOR_NULL=1 monado-service &
      XR_RUNTIME_JSON=.../share/openxr/1/openxr_monado.json fa18 --vr ORIGINAL.adf

  Done on 2026-11-12 with Monado 25.1 for GLX (X11 window) and EGL
  (`SDL_VIDEODRIVER=offscreen`): session, both swapchains, a flight and
  the menus, clean exit.  **Not yet tried on a real headset or with
  SteamVR.**

## Open

- Depth: stereo and head position for near objects.  The view-space units
  of the captured primitives differ per object (level-of-detail scaling,
  `$328ea`: 2^(8 - fp-6) world units); 672 world units are a metre
  (1024 units = 5 ft).  The capture has to record the unit per primitive.
- A 3D cockpit instead of the flat screen.
- Controllers (OpenXR actions), Windows, OpenGL ES.
- The stats / fps overlay is only in the window.
- With a swapchain format that is not sRGB (none seen) colours are too
  bright.
