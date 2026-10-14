// Remaster hooks that are called from translated routines; no-ops unless
// a remaster feature that needs them is enabled.
#pragma once

namespace remaster {
extern bool scene_hooks;   // set by install()
void scene_begin();        // start of the 3D scene draw ($2fc14)
void scene_end();          // end of the 3D scene draw
}  // namespace remaster
