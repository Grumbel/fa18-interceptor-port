#pragma once
#include <SDL.h>
#include <algorithm>
#include <cstdint>
#include "display.hpp"
#include "gfx_state.hpp"

namespace fa18 {

// Draw modes recovered from move.w #MODE, $36e / jsr $12 pattern.
// See docs/draw_modes.md.
enum class DrawMode : uint16_t {
  Span    = 0x01,  // DDA span/poly edge
  Line    = 0x1b,  // blitter line (sole issuer 0x28274)
  VtxWalk = 0x16,  // projected vertex walker entry
  // 42 additional modes catalogued in docs/draw_modes.md
};

inline void clamp_to_playfield(int& x, int& y) {
  x = std::max(0, std::min(PLAYFIELD_W - 1, x));
  y = std::max(0, std::min(PLAYFIELD_H - 1, y));
}

inline void draw_line(SDL_Renderer* ren, int x0, int y0, int x1, int y1,
                      Uint8 r = 255, Uint8 g = 255, Uint8 b = 255) {
  clamp_to_playfield(x0, y0);
  clamp_to_playfield(x1, y1);
  SDL_SetRenderDrawColor(ren, r, g, b, 255);
  SDL_RenderDrawLine(ren, x0 * SCALE, y0 * SCALE, x1 * SCALE, y1 * SCALE);
}

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

// Mirrors: move.w #MODE, $36e / jsr $12
inline void dispatch_draw(GfxState& gfx, DrawMode mode) {
  gfx.draw_mode = static_cast<uint16_t>(mode);
  // Runtime $12 dispatcher would switch on mode here.
}

// $5ee span helper (docs/func_5ee.md): normalise endpoint order, reject
// zero-length / out-of-limit spans.  Returns false if the span should be
// skipped.  On success, x0 <= x1 and dx is the inclusive pixel count - 1.
inline bool span_setup(int& x0, int& x1, int limit, int& dx) {
  if (x0 == x1)
    return false;  // zero-length
  if (x1 < x0)
    std::swap(x0, x1);  // path_lo → path_hi order
  // Original: addq #1, start; cmp limit; bgt skip
  if (x0 + 1 > limit)
    return false;
  dx = x1 - x0 - 1;
  if (dx < 0)
    return false;
  // Bitplane scale ×40 and BLTCON bit-mask setup omitted (SDL backend)
  return true;
}

// Mode $1b line path (hunk 35 entry + BLTCON0 $09f0 setup).
// Calls $5ee-style span_setup for horizontal rejection, then draws.
inline void blitter_line_stub(SDL_Renderer* ren, GfxState& gfx,
                              int x0, int y0, int x1, int y1) {
  dispatch_draw(gfx, DrawMode::Line);
  // Horizontal span fast-path mirrors $5ee when y0 == y1
  if (y0 == y1) {
    int dx = 0;
    if (!span_setup(x0, x1, PLAYFIELD_W - 1, dx))
      return;
  }
  draw_line(ren, x0, y0, x1, y1);
}

// Mode $1b wrapper (file 0x28274): force line mode and dispatch.
inline void mode_1b_line(GfxState& gs) {
  gs.draw_mode = 0x1b;
  // jsr $12 → draw engine reads gs.draw_mode
}
} // namespace fa18
