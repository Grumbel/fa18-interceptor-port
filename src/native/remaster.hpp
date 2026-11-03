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
}  // namespace remaster
