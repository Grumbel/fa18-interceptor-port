#pragma once
#include <SDL.h>
#include <algorithm>
#include "display.hpp"
#include "gfx_state.hpp"

namespace fa18 {

inline void clamp_to_playfield(int& x, int& y) {
  x = std::max(0, std::min(PLAYFIELD_W - 1, x));
  y = std::max(0, std::min(PLAYFIELD_H - 1, y));
}

// Recovered from hunk-35 clamp using constants 160/320/90/180
// (file ~0x28294+).  Coordinates are playfield-space; rendered at SCALE.
inline void draw_line(SDL_Renderer* ren, int x0, int y0, int x1, int y1,
                      Uint8 r = 255, Uint8 g = 255, Uint8 b = 255) {
  clamp_to_playfield(x0, y0);
  clamp_to_playfield(x1, y1);
  SDL_SetRenderDrawColor(ren, r, g, b, 255);
  SDL_RenderDrawLine(ren, x0 * SCALE, y0 * SCALE, x1 * SCALE, y1 * SCALE);
}

// Placeholder for the area-fill path that uses BLTCON0 family
// $0dfc / $0fec / $0f4c (file ~0x29b66+).
inline void draw_fill(SDL_Renderer* ren, int x, int y, int w, int h,
                      Uint8 r = 80, Uint8 g = 80, Uint8 b = 120) {
  int x1 = x + w - 1, y1 = y + h - 1;
  clamp_to_playfield(x, y);
  clamp_to_playfield(x1, y1);
  if (x1 < x || y1 < y) return;
  SDL_Rect rc{x * SCALE, y * SCALE, (x1 - x + 1) * SCALE, (y1 - y + 1) * SCALE};
  SDL_SetRenderDrawColor(ren, r, g, b, 255);
  SDL_RenderFillRect(ren, &rc);
}

/*
 * Register mapping recovered for the primary line/fill entry
 * (file 0x29a88, BLTCON0=$09f0):
 *
 *   After coordinate prep the routine stores:
 *     d1 → $330, $334, $338   (intermediate pointers / values)
 *     d7 → $33e               (size word)
 *   Then programs the blitter:
 *     d2 → BLTAPT / BLTBPT ($50/$54)
 *     d0 → BLTCPT           ($44)
 *     d3 → BLTAMOD/BLTCMOD/BLTDMOD ($64/$62/$66)
 *     d7 → BLTSIZE          ($58)
 *
 * Exact geometric meaning of each Dn (start/end vs delta) still TBD;
 * the clamp + these stubs are sufficient for the port to render
 * wireframe geometry once the 3D projector is identified.
 */
inline void blitter_line_stub(SDL_Renderer* ren, GfxState& gfx,
                              int x0, int y0, int x1, int y1) {
  // Mirror the mode write seen at hunk-35 entry
  gfx.draw_mode = 0x1b;
  draw_line(ren, x0, y0, x1, y1);
}

} // namespace fa18
