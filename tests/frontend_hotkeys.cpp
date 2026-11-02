// Standalone regression: c++ -std=c++20 -Isrc tests/frontend_hotkeys.cpp -o /tmp/fa18-hotkeys
#include "frontend_hotkeys.hpp"
#include <cassert>

int main() {
  using K = FrontendKey;
  using A = FrontendAction;
  FrontendHotkeys h;

  // The game's high throttle and afterburner keys must pass through intact.
  assert(h.key(K::F9, true, false, false) == A::PassToGame);
  assert(h.key(K::F9, false, false, false) == A::PassToGame);
  assert(h.key(K::F10, true, false, false) == A::PassToGame);
  assert(h.key(K::F10, false, false, false) == A::PassToGame);

  // A host combo consumes both halves and does not repeat while held.
  assert(h.key(K::F9, true, true, false) == A::OpenMenu);
  assert(h.key(K::F9, true, true, true) == A::Consumed);
  assert(h.key(K::F9, false, false, false) == A::Consumed);
  assert(h.key(K::F10, true, true, false) == A::OpenModels);
  assert(h.key(K::F10, false, false, false) == A::Consumed);

  // A menu may eat the release; the next plain key-down resets suppression.
  assert(h.key(K::F10, true, true, false) == A::OpenModels);
  assert(h.key(K::F10, true, false, false) == A::PassToGame);
  assert(h.key(K::F10, false, false, false) == A::PassToGame);
  assert(h.key(K::Other, true, true, false) == A::PassToGame);
}
