// Remaster hooks that are called from translated routines; no-ops unless
// a remaster feature that needs them is enabled.
#pragma once

namespace amiga { struct Machine; }

namespace remaster {
extern bool scene_hooks;   // set by install()
void install(amiga::Machine& m);  // PC hooks for prim observation + hires layer
void scene_begin();        // start of the 3D scene draw ($2fc14)
void scene_end();          // end of the 3D scene draw
void sky_quad_done(bool ok);  // $42f70 returns (the RTS $432aa / $432b6)
}  // namespace remaster
