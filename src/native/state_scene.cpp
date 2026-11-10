#include "native/state_scene.hpp"

namespace state_scene {

Vec3f model_to_view(const std::array<int16_t, 9>& view_m, const int32_t obj_pos[3],
                    const int32_t cam_pos[3], const int32_t v[3], float model_scale) {
    float a[3];
    for (int i = 0; i < 3; ++i)
        a[i] = float(v[i]) / model_scale + float(int64_t(obj_pos[i]) - cam_pos[i]) / 256.0f;
    float o[3];
    for (int r = 0; r < 3; ++r)
        o[r] = (a[0] * view_m[r * 3] + a[1] * view_m[r * 3 + 1] + a[2] * view_m[r * 3 + 2]) / 256.0f;
    return {o[0], o[1], o[2]};
}

}  // namespace state_scene
