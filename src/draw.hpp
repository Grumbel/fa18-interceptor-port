#pragma once
#include <SDL.h>
#include <algorithm>
#include "display.hpp"

namespace fa18 {

inline void clamp_to_playfield(int& x, int& y) {
  x = std::max(0, std::min(PLAYFIELD_W - 1, x));
  y = std::max(0, std::min(PLAYFIELD_H - 1, y));
}

// Placeholder for the hunk-35 clamp + draw pipeline.
// Coordinates are in playfield space (320×180); rendered at 2×.
inline void draw_line(SDL_Renderer* ren, int x0, int y0, int x1, int y1,
                      Uint8 r = 255, Uint8 g = 255, Uint8 b = 255) {
  clamp_to_playfield(x0, y0);
  clamp_to_playfield(x1, y1);
  SDL_SetRenderDrawColor(ren, r, g, b, 255);
  SDL_RenderDrawLine(ren, x0 * SCALE, y0 * SCALE, x1 * SCALE, y1 * SCALE);
}

} // namespace fa18
