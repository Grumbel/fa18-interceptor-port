// Head-mounted display for `--renderer new` (`--vr`): an OpenXR session on
// the window's OpenGL context, and the framebuffers the two eyes are drawn
// into.
//
// Per picture: begin_frame() waits for the headset's next display time and
// gives the head and the eyes for it (vr_math.hpp); the caller builds one
// display list for the head (Shadow::View::head) and draws it once per eye
// between begin_eye() / end_eye(); end_frame() hands the pictures to the
// headset.  mirror() shows them in the window.
//
// The scene is seen from each eye's own place (Prim::unit gives the
// distances): stereo depth, and the head's movements relative to where it
// was when the view was centred.  The game's screen (cockpit, HUD) is a
// picture at infinity (see docs/vr.md).
//
// Without OpenXR (not built in, or Options::simulate) the same path runs
// with a fixed head and both eyes go to the window only; tests use that.
#pragma once

#include "render/gl_scene.hpp"
#include "render/vr_math.hpp"

#include <memory>
#include <string>

namespace render {

class Vr {
 public:
  struct Options {
    bool simulate = false;
    // simulated head, degrees: turned right, up, rolled clockwise; field of
    // view per eye and the size of an eye's picture
    float yaw = 0, pitch = 0, roll = 0, fov = 90;
    int eye_size = 1024;
    int samples = 4;  // multisampling of the eyes' pictures
    // Stereo: false puts both eyes at the head's centre (everything at
    // infinity, as before).  The simulated eyes are `ipd` metres apart.
    bool stereo = true;
    float ipd = 0.064f;
  };

  Vr();
  ~Vr();
  // True if this build can talk to a headset (OpenXR).
  static bool has_openxr();
  // The window's OpenGL context must be current.
  bool init(GetProc proc, const Options& opt, std::string& err);
  // End the session; to be called while the OpenGL context still exists.
  void shutdown();
  bool simulated() const;
  // One line for the log: runtime, headset, picture size.
  std::string description() const;

  enum class Step {
    Idle,    // no session is running (headset off, runtime not ready): draw the window as usual
    Skip,    // a frame was waited for and ended, but nothing is to be drawn (not visible)
    Render,  // draw both eyes, then end_frame()
  };
  Step begin_frame(HeadView& head);
  void begin_eye(int eye);  // binds the eye's framebuffer
  void end_eye(int eye);
  void end_frame();
  // The eyes' pictures into the window's framebuffer (bound afterwards).
  void mirror(int win_w, int win_h);

  // True while begin_frame() waits for the headset's display (it paces the loop).
  bool paced() const;
  // The runtime asked the program to end.
  bool exit_requested() const;
  // Take the direction the head looks in now as straight ahead.
  void recentre();

 private:
  struct Impl;
  std::unique_ptr<Impl> p_;
};

}  // namespace render
