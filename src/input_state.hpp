#pragma once
#include <cstdint>

// Mirrors low-memory input state recovered from the Amiga binary.
// See docs/keyboard.md.

namespace fa18 {

struct InputState {
  uint8_t  key_code      = 0;    // $155  (0 = none)
  bool     input_enabled = true; // $206
  uint8_t  input_mode    = 0;    // $207
  uint16_t menu_index    = 0;    // $2ac
};

} // namespace fa18
