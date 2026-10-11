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
  // Recovered from stores immediately before the $09f0 setup at file 0x29a88.
  uint32_t blt_param0 = 0;       // $330
  uint32_t blt_param1 = 0;       // $334
  uint32_t blt_param2 = 0;       // $338
  uint16_t blt_size   = 0;       // $33e  – ultimately fed to BLTSIZE
};

} // namespace fa18
