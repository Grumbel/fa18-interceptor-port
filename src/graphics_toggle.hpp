// Front-end graphics-mode switch; deliberately independent of SDL and GL.
// The next enhanced mode survives a temporary return to original pixels.
#pragma once

struct GraphicsMode {
  int scale = 0;
  bool opengl = false;
};

class GraphicsToggle {
 public:
  // Starting with original graphics: Ctrl+F11 first requests 3x OpenGL.
  // The host may downgrade this to software hi-res if GL cannot initialize.
  GraphicsMode remembered() const { return remembered_; }

  void remember(GraphicsMode mode) {
    if (mode.scale > 0) remembered_ = mode;
  }

  GraphicsMode next(GraphicsMode current) {
    if (current.scale > 0) {
      remember(current);
      return {};
    }
    return remembered_;
  }

 private:
  GraphicsMode remembered_{3, true};
};
