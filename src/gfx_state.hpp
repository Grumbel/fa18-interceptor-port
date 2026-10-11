#pragma once
#include <cstdint>

// Mirrors low-memory graphics state recovered from the Amiga binary.
// See docs/lowmem_vars.md and docs/blitter_hunk35.md.

namespace fa18 {

struct GfxState {
  // Core draw control
  uint16_t draw_mode  = 0;       // $36e  – operation / mode ID
  uint16_t pen        = 0;       // $324  – plane / colour select
  uint32_t plane_mask = 0xfffff; // $b6   – bitplane enable
  int32_t  sentinel   = -1;      // $328  – invalid / last-result
  uint16_t phase      = 0;       // $112  – high-level screen phase

  // Blitter parameter scratch (written by prep code before BLTSIZE)
  uint32_t blt_param0 = 0;       // $330  – dest address
  uint32_t blt_param1 = 0;       // $334  – copy of dest
  uint32_t blt_param2 = 0;       // $338  – copy of dest
  uint16_t blt_size   = 0;       // $33e  – BLTSIZE = (h<<6)|w

  // Line parameter block ($34c–$352)
  int16_t line_x0 = 0;           // $34c (spilled)
  int16_t line_x1 = 0;           // $34e (spilled)
  int16_t line_y0 = 0;           // $350
  int16_t line_y1 = 0;           // $352

  // Bitplane base pointer
  uint32_t bitplane_base = 0;    // $b2

  // View/camera translation applied after matrix multiply
  // (absolute $b68 / $b6c / $b70).
  int32_t view_x = 0;            // $b68 (masked & $3fffff in original)
  int32_t view_y = 0;            // $b6c
  int32_t view_z = 0;            // $b70
};

} // namespace fa18
