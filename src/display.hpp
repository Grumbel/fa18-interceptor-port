#pragma once

// Display constants recovered from the original Amiga binary
// (blitter modulo = 40 bytes → 320 px; clamp constants 320×180).

namespace fa18 {
  constexpr int PLAYFIELD_W = 320;
  constexpr int PLAYFIELD_H = 180;
  constexpr int PLAYFIELD_CX = 160;
  constexpr int PLAYFIELD_CY = 90;
  constexpr int WINDOW_W = 640;
  constexpr int WINDOW_H = 400;
  constexpr int SCALE = 2;  // window is 2× playfield
}
