#pragma once
#include <cstdint>
#include <array>

// Display constants recovered from the original Amiga binary
// (blitter modulo = 40 bytes → 320 px; clamp constants 320×180).
// Palette and copper values from docs/struct_display_ctx.md.

namespace fa18 {

constexpr int PLAYFIELD_W = 320;
constexpr int PLAYFIELD_H = 180;   // active draw height (copper enables to ~200)
constexpr int PLAYFIELD_CX = 160;
constexpr int PLAYFIELD_CY = 90;
constexpr int WINDOW_W = 640;
constexpr int WINDOW_H = 400;
constexpr int SCALE = 2;  // window is 2× playfield

// Copper / hardware display (live list at Chip $0000A400)
constexpr uint16_t kDiwStrt  = 0x0581;
constexpr uint16_t kDiwStop  = 0x40C1;
constexpr uint16_t kDdfStrt  = 0x0038;
constexpr uint16_t kDdfStop  = 0x00D0;
constexpr uint16_t kBplCon0_off = 0x0200;  // COLOR only, 0 planes (blanking)
constexpr uint16_t kBplCon0_on  = 0x4200;  // COLOR + 4 bitplanes (visible)
constexpr uint16_t kBplCon2  = 0x0024;
constexpr int kActivePlanes = 4;           // copper programs BPL1–4PT
constexpr int kPlaneBytes = 320 / 8 * 200; // 8000 bytes per plane

// Primary 16-colour palette from register-script / ColorTable $00C069F8.
// Values are 12-bit Amiga RGB (4 bits per channel): 0x0RGB.
constexpr std::array<uint16_t, 16> kPaletteAmiga = {{
  0x0000,  // COLOR00 black
  0x0800,  // COLOR01
  0x0888,  // COLOR02
  0x0111,  // COLOR03
  0x0151,  // COLOR04
  0x0447,  // COLOR05
  0x0036,  // COLOR06
  0x0444,  // COLOR07
  0x0555,  // COLOR08
  0x0D92,  // COLOR09
  0x0666,  // COLOR10
  0x0030,  // COLOR11
  0x0333,  // COLOR12
  0x0777,  // COLOR13
  0x0777,  // COLOR14
  0x0333,  // COLOR15
}};

// Convert 12-bit Amiga 0x0RGB → 24-bit 0xRRGGBB (expand 4→8 by replication).
inline uint32_t amiga_to_rgb24(uint16_t amiga) {
  uint32_t r = (amiga >> 8) & 0xF;
  uint32_t g = (amiga >> 4) & 0xF;
  uint32_t b =  amiga       & 0xF;
  r |= r << 4;
  g |= g << 4;
  b |= b << 4;
  return (r << 16) | (g << 8) | b;
}

inline std::array<uint32_t, 16> palette_rgb24() {
  std::array<uint32_t, 16> out{};
  for (int i = 0; i < 16; ++i)
    out[i] = amiga_to_rgb24(kPaletteAmiga[i]);
  return out;
}

}  // namespace fa18
