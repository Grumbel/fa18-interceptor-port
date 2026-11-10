// Checks state_scene::model_to_view against values captured from the game
// (free1 replay, frame 3966, type 86 tower; see docs/state_renderer.md).
#include "native/state_scene.hpp"

#include <cmath>
#include <cstdio>

int main() {
    const std::array<int16_t, 9> m{148, 0, 78, 0, 252, 0, -60, 0, 113};
    // player pos (see docs): eye 5209,7,9220 -> use eye*256 as world pos
    const int32_t cam[3] = {5209 * 256, 7 * 256, 9220 * 256};
    const int32_t obj_w[3] = {5632 * 256, 0, 12544 * 256};
    const int32_t v[3] = {448, 0, -448};
    auto p = state_scene::model_to_view(m, obj_w, cam, v);
    // captured (1272,-13,1319); rounding of eye/pos accounts for y,z residue
    int fail = 0;
    if (std::fabs(p.x - 1272) > 3) fail = 1;
    if (std::fabs(p.y - (-13)) > 8) fail = 1;
    if (std::fabs(p.z - 1319) > 14) fail = 1;
    std::printf("state_scene: %s (%.1f %.1f %.1f)\n", fail ? "FAIL" : "OK", p.x, p.y, p.z);
    return fail;
}
