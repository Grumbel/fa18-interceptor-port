#pragma once
#include <cstdint>

// Mirrors low-memory graphics state recovered from the Amiga binary.
// See docs/lowmem_vars.md.

namespace fa18 {

struct GfxState {
  uint16_t draw_mode  = 0;       // $36e
  uint16_t pen        = 0;       // $324
  uint32_t plane_mask = 0xfffff; // $b6  (default full enable)
  int32_t  sentinel   = -1;      // $328
  uint16_t phase      = 0;       // $112
};

} // namespace fa18
