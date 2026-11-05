// Remaster hooks that are called from translated routines; no-ops unless
// a remaster feature that needs them is enabled.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace amiga { struct Machine; }

namespace remaster {
extern bool scene_hooks;   // set by install()
void install(amiga::Machine& m);  // PC hooks for prim observation + hires layer
void scene_begin();        // start of the 3D scene draw ($2fc14)
void scene_end();          // end of the 3D scene draw
void sky_quad_done(bool ok);  // $42f70 returns (the RTS $432aa / $432b6)
// Recompose a cached GL scene without running the emulated CPU. `side` is
// the number of extra source-resolution columns per edge; alpha=0..1 blends
// previous/current 3D captures while keeping the game's 2D overlay intact.
// `refresh_center` also substitutes blended geometry into the original 4:3
// region; otherwise (widescreen only) its pixels remain exactly unchanged.
bool compose_view(amiga::Machine& m, std::vector<uint32_t>& out, int side,
                  float alpha, bool refresh_center);
// The 68000 menu-state addresses for the flight Pause overlay. Used to
// replace its low-resolution heading with a host-rendered 2D UI layer.
bool pause_screen_active();

// Original HUD number cells, in source-screen pixels. The old 3x5 glyphs
// serve as a value/visibility oracle until their drawing logic is recovered.
struct HudDigitSlot {
  int x, y, background_x, group;
};
inline constexpr std::array<HudDigitSlot, 12> kHudDigitSlots{{
    {140, 53, 135, 0}, {144, 53, 135, 0},
    {160, 53, 156, 0}, {164, 53, 156, 0},
    {98, 84, 94, 1}, {102, 84, 94, 1}, {106, 84, 94, 1},
    {218, 84, 215, 2}, {222, 84, 215, 2}, {226, 84, 215, 2},
    {96, 121, 93, 3}, {102, 121, 93, 3},
}};

// The five fixed flight-unit labels are drawn with the same 3x5 font as
// the numbers. Only exact source-screen glyphs are replaced; the cockpit
// and alternate cameras retain their original text.
struct HudLetterSlot {
  int x, y, background_x;
  char letter;
};
inline constexpr std::array<HudLetterSlot, 5> kHudLetterSlots{{
    {102, 91, 94, 'K'}, {106, 91, 94, 'T'}, // airspeed (KT)
    {222, 91, 215, 'F'}, {226, 91, 215, 'T'}, // altitude (FT)
    {108, 121, 93, 'G'},                       // G-meter
}};

// The crosshair and frame source pixels only supply visibility and live
// colour; the digit cells additionally decode the old glyph shape into a
// native seven-stroke digit. The reticle tracks the copper Pause fade.
struct NativeHudState {
  bool pause_title = false;
  uint32_t pause_rgb = 0;
  bool reticle = false;
  bool frame_crown = false;
  bool frame_rails = false;
  uint32_t reticle_rgb = 0;
  uint32_t frame_top_rgb = 0;
  uint32_t frame_corner_rgb = 0;
  uint32_t frame_rail_rgb = 0;
  // 0 = no validated digit; otherwise seven-stroke mask (128 = centre 1).
  std::array<uint8_t, kHudDigitSlots.size()> digit_strokes{};
  std::array<uint32_t, 4> digit_colours{};
  std::array<uint8_t, kHudLetterSlots.size()> letter_visible{};
  std::array<uint32_t, kHudLetterSlots.size()> letter_colours{};
  bool heading_marks = false;
  int heading_first_x = 0;
  uint32_t heading_rgb = 0;
};
NativeHudState native_hud_state(const amiga::Machine& m);
// The native GL presenter calls this after prepare_gpu_layers(). Keep it
// outside GpuLayers so the old SDL host's POD layout stays unchanged.
NativeHudState prepared_gpu_hud_state();

// Separate the original Amiga screen-space drawing (cockpit, HUD, text and
// otherwise unexplained pixels) from the captured 3D scene. The GL presenter
// uses these small 320x200/32x200 textures to composite on the GPU instead
// of reading a multi-megapixel scene back to the CPU each presentation.
// Overlay alpha: 255 = original RGBA/dynamic pixel, 48 = cockpit asset RGB,
// 32 = cockpit palette index, 16 = scene with EHB bit set, 0 = ordinary scene.
// Row RGBA: scene row, validity, left/right EHB bits.
struct GpuLayers {
  std::vector<uint8_t> overlay, palette, rows;
  int scene_rows = 0;
  // Five independent source glyph values already latched for the displayed
  // fuel-LCD bank. The GL pass draws native strokes over their cleared panel.
  std::array<uint8_t, 5> fuel_strokes{};
  uint32_t fuel_ink_rgb = 0;
  bool fuel_vector = false;
  // Native vertical fuel gauge, cleared from the source-pixel overlay and
  // drawn by the same GPU pass with no additional texture or draw call.
  uint32_t fuel_bar_ink_rgb = 0;
  int fuel_bar_rows = 0;
  bool fuel_bar_vector = false;
  // Central radar: fixed guides/marker and one live-contact position. The
  // presenter generates these shapes directly, no extra sampler or draw.
  bool radar_vector = false;
  uint32_t radar_guide_rgb = 0, radar_marker_rgb = 0, radar_dot_rgb = 0;
  int radar_dot_x = -1, radar_dot_y = -1;
};
bool prepare_gpu_layers(amiga::Machine& m, GpuLayers& out);
}  // namespace remaster
