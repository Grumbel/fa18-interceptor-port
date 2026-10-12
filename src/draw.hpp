#pragma once
#include <SDL.h>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <vector>
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

// Draw / clear with recovered Amiga palette index (COLOR00–15).
inline void draw_line_pal(SDL_Renderer* ren, int x0, int y0, int x1, int y1,
                          int color_idx) {
  uint32_t rgb = amiga_to_rgb24(kPaletteAmiga[color_idx & 15]);
  draw_line(ren, x0, y0, x1, y1,
            static_cast<Uint8>((rgb >> 16) & 0xFF),
            static_cast<Uint8>((rgb >> 8) & 0xFF),
            static_cast<Uint8>(rgb & 0xFF));
}

inline void clear_pal(SDL_Renderer* ren, int color_idx) {
  uint32_t rgb = amiga_to_rgb24(kPaletteAmiga[color_idx & 15]);
  SDL_SetRenderDrawColor(ren,
                         static_cast<Uint8>((rgb >> 16) & 0xFF),
                         static_cast<Uint8>((rgb >> 8) & 0xFF),
                         static_cast<Uint8>(rgb & 0xFF), 255);
  SDL_RenderClear(ren);
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

// DDA edge step with symmetric remainder rounding (live $00C23318).
// After muls/divs the remainder is compared to |denom|/2; if larger the
// quotient is nudged ±1.  See docs/draw_modes.md.
inline int16_t dda_rounded_div(int32_t numer, int16_t denom) {
  if (denom == 0) return 0;
  int16_t q = static_cast<int16_t>(numer / denom);
  int16_t r = static_cast<int16_t>(numer % denom);
  int16_t half = static_cast<int16_t>(std::abs(static_cast<int>(denom)) / 2);
  if (std::abs(static_cast<int>(r)) > half)
    q = static_cast<int16_t>(q + (r > 0 ? 1 : -1));
  return q;
}

// One axis of the $00C23318 / $00C2340A pair: advance cur by rounded
// (delta_num * scale) / delta_den.
inline int16_t dda_axis_step(int16_t cur, int16_t delta_num,
                             int16_t delta_den, int16_t scale) {
  return static_cast<int16_t>(
      cur + dda_rounded_div(int32_t(delta_num) * scale, delta_den));
}

// Edge list at live $00C4ACC8 (docs/draw_modes.md):
//   header.w, count.w, count × (x,y,z).w  — view-space, ×8 in DDA body.
struct EdgeVertex {
  int16_t x = 0, y = 0, z = 0;
};

struct EdgeList {
  int16_t header = 0;
  std::vector<EdgeVertex> verts;
};

// Shared DDA body $00C231F8: walk consecutive vertex pairs.
// Full octant/sign logic still simplified — draws polyline for now.
inline void dda_edge_list(SDL_Renderer* ren, const EdgeList& el,
                          Uint8 r = 180, Uint8 g = 220, Uint8 b = 255) {
  if (el.verts.size() < 2) return;
  for (size_t i = 1; i < el.verts.size(); ++i) {
    int x0 = el.verts[i - 1].x / 8;  // undo ×8 for SDL preview
    int y0 = el.verts[i - 1].y / 8;
    int x1 = el.verts[i].x / 8;
    int y1 = el.verts[i].y / 8;
    // Centre view-space origin roughly on playfield mid
    x0 = x0 / 16 + PLAYFIELD_W / 2;
    y0 = y0 / 16 + PLAYFIELD_H / 2;
    x1 = x1 / 16 + PLAYFIELD_W / 2;
    y1 = y1 / 16 + PLAYFIELD_H / 2;
    draw_line(ren, x0, y0, x1, y1, r, g, b);
  }
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

// ---------------------------------------------------------------------------
// $4202 edge descriptor + walker / perspective (docs/draw_modes.md)
// ---------------------------------------------------------------------------

// Clip bounds at +6 / +8 / +$a of the live $4202 block ($00C4B2CA).
struct EdgeDesc {
  int16_t bound_a = 0;  // +6
  int16_t bound_b = 0;  // +8
  int16_t bound_c = 0;  // +$a
};

// Perspective screen bias recovered from file 0x28660:
//   addi.w #$5A, d4   (+90  Y centre)
//   cmpi.w #$B4, d4   (180  Y bound)
//   subi.w #$13F, d3  (−319 X centre ≈ 320-wide)
struct ScreenBias {
  static constexpr int CX = 319;      // $13F
  static constexpr int CY = 90;       // $5A
  static constexpr int Y_BOUND = 180; // $B4
};

// Edge interpolate + perspective (file 0x286CA / 0x286F8).
// Pre-perspective coords in, screen sx/sy out.  Returns false if rejected.
inline bool perspective_edge(int16_t x0, int16_t y0, int16_t z_delta,
                             int16_t& sx, int16_t& sy) {
  if (z_delta == 0)
    return false;
  int x = x0;
  int y = y0;
  y += ScreenBias::CY;
  if (y < 0 || y >= ScreenBias::Y_BOUND)
    return false;
  x = ScreenBias::CX - x;  // subi #$13F / neg pattern
  y = ScreenBias::Y_BOUND - 1 - y;
  sx = static_cast<int16_t>(x);
  sy = static_cast<int16_t>(y);
  return true;
}

// Walker clip test (file 0x2844E): compare vertex pair against EdgeDesc bounds.
inline bool walker_clip(int16_t v0, int16_t v1, const EdgeDesc& ed) {
  if (v0 < v1) {
    if (ed.bound_c <= -ed.bound_a)
      return false;
  } else {
    if (ed.bound_c <= ed.bound_a)
      return false;
  }
  return true;
}

// Mode $16 path: walk edge list → perspective → span/line.
// verts: array of (x,y) pairs; nverts = number of vertices.
inline void walker_draw(SDL_Renderer* ren, GfxState& gfx,
                        const int16_t* verts, int nverts,
                        const EdgeDesc& ed) {
  dispatch_draw(gfx, DrawMode::VtxWalk);
  if (nverts < 2)
    return;
  for (int i = 0; i < nverts; ++i) {
    int i1 = (i + 1) % nverts;
    int16_t x0 = verts[i * 2], y0 = verts[i * 2 + 1];
    int16_t x1 = verts[i1 * 2], y1 = verts[i1 * 2 + 1];
    if (!walker_clip(x0, x1, ed))
      continue;
    int16_t sx0, sy0, sx1, sy1;
    if (!perspective_edge(x0, y0, 1, sx0, sy0))
      continue;
    if (!perspective_edge(x1, y1, 1, sx1, sy1))
      continue;
    blitter_line_stub(ren, gfx, sx0, sy0, sx1, sy1);
  }
}
} // namespace fa18
