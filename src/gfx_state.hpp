#pragma once
#include <cstdint>

// Mirrors low-memory graphics state recovered from the Amiga binary.
// See docs/lowmem_vars.md and docs/transform_3d.md.

namespace fa18 {

struct GfxState {
  // Core draw control
  uint16_t draw_mode  = 0;       // $36e  – operation / mode ID
  uint16_t pen        = 0;       // $324  – plane / colour select
  uint32_t plane_mask = 0xfffff; // $b6   – bitplane enable
  int32_t  sentinel   = -1;      // $328  – invalid / last-result
  uint16_t phase      = 0;       // $112  – high-level screen phase

  // Blitter parameter scratch (written by prep code before BLTSIZE)
  uint32_t blt_param0 = 0;       // $330
  uint32_t blt_param1 = 0;       // $334
  uint32_t blt_param2 = 0;       // $338
  uint16_t blt_size   = 0;       // $33e

  // View/camera translation applied after matrix multiply
  // (absolute $b68 / $b6c / $b70).
  int32_t view_x = 0;            // $b68 (masked & $3fffff in original)
  int32_t view_y = 0;            // $b6c
  int32_t view_z = 0;            // $b70
};

} // namespace fa18
