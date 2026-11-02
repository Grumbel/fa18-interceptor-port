#include "graphics_toggle.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

int main() {
  GraphicsToggle t;
  auto check = [](GraphicsMode m, int scale, bool gl) {
    assert(m.scale == scale && m.opengl == gl);
  };

  // Booting without enhancements: first toggle enables GL 3x.
  check(t.next({0, false}), 3, true);

  // Toggling off remembers the *actual* active mode, not the default.
  check(t.next({4, true}), 0, false);
  check(t.next({0, false}), 4, true);
  check(t.next({2, false}), 0, false);
  check(t.next({0, false}), 2, false);

  // Explicit resolution changes in the menu replace the remembered mode.
  t.remember({3, true});
  check(t.next({0, false}), 3, true);
  t.remember({0, false});  // selecting original never erases the last choice
  check(t.next({0, false}), 3, true);
  t.remember({4, false});  // GL initialization failure fallback
  check(t.next({0, false}), 4, false);
}
