#pragma once
#include <cstdint>

// Mirrors low-memory input state recovered from the Amiga binary.
// See docs/keyboard.md and docs/lowmem_vars.md (attitude decoder).

namespace fa18 {

struct InputState {
  // Keyboard path ($155 / $206 / $207 / $2ac)
  uint8_t  key_code      = 0;    // $155  (0 = none)
  bool     input_enabled = true; // $206
  uint8_t  input_mode    = 0;    // $207
  uint16_t menu_index    = 0;    // $2ac

  // Attitude / control axes (file 0x404c decoder inputs)
  // Original range 0–255; centre ≈ 128.
  uint8_t  axis_0 = 128;         // $4b0  → pitch (angle1 / Ry)
  uint8_t  axis_1 = 128;         // $4b2  → yaw   (angle2 / Rz)
  uint16_t status_29c = 0;       // $29c  sticky flags

  // Convert 0–255 axis to 7200-circle Euler units (port convention).
  // Centre 128 → 0; full deflection ≈ ±90° = ±1800 units.
  static int axis_to_euler(uint8_t axis) {
    int delta = int(axis) - 128;          // -128..127
    return delta * 1800 / 128;            // ≈ -1800..1785
  }
};

} // namespace fa18
