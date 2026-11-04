// Remaster hooks that are called from translated routines; no-ops unless
// a remaster feature that needs them is enabled.
#pragma once

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

// Source-screen pixels are used only as a visibility/colour timing oracle
// for independently drawn high-resolution vector UI, never as glyph shapes.
// The reticle stays in place while the game's copper palette fades on Pause.
struct NativeHudState {
  bool pause_title = false;
  bool reticle = false;
  bool frame_crown = false;
  bool frame_rails = false;
  uint32_t reticle_rgb = 0;
  uint32_t frame_top_rgb = 0;
  uint32_t frame_corner_rgb = 0;
  uint32_t frame_rail_rgb = 0;
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
};
bool prepare_gpu_layers(amiga::Machine& m, GpuLayers& out);
}  // namespace remaster
