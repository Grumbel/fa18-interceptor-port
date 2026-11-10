#include "native/state_scene.hpp"

#include <cmath>
#include <cstdlib>

namespace state_scene {

namespace {
const std::array<int16_t, 9>& identity_basis() {
    static const std::array<int16_t, 9> m{16384, 0, 0, 0, 16384, 0, 0, 0, 16384};
    return m;
}

// Conservative frustum test with the game's projection
// (sx = 159.5 - 160 vx/vz, sy = 89.5 - 90 vy/vz): drop primitives entirely on
// one outer side of a plane through the eye (z > 0 is in front).
bool g_cull = true;
int next_object_id = 0;
bool visible(const std::vector<Vec3f>& v) {
    if (!g_cull) return true;
    bool any_front = false, left = true, right = true, up = true, down = true;
    for (const auto& p : v) {
        any_front |= p.z > 0;
        left &= 159.5f * p.z - 160.0f * p.x < 0;           // sx < 0
        right &= 159.5f * p.z - 160.0f * p.x > 320.0f * p.z;  // sx > 320
        up &= 89.5f * p.z - 90.0f * p.y < 0;                // sy < 0
        down &= 89.5f * p.z - 90.0f * p.y > 180.0f * p.z;   // sy > 180
    }
    return any_front && !left && !right && !up && !down;
}
}  // namespace

void set_frustum_cull(bool on) { g_cull = on; }

std::vector<ViewPrim> build_static(const gamestate::GameState& gs, const world::World& w,
                                   const std::vector<mesh_viewer::Model>& models,
                                   int radius_cells) {
    std::vector<ViewPrim> out;
    if (gs.player < 0) return out;
    const int32_t cam[3] = {gs.camera.eye.x, gs.camera.eye.y, gs.camera.eye.z};
    for (int map_id = 0; map_id < world::kMaps; ++map_id) {
    if (!w.has_map[map_id]) continue;
    const int cell_shift = map_id == world::Near ? 22 : 24;   // far maps: 4x4-cell sectors
    const int ccx = cam[0] >> cell_shift, ccz = cam[2] >> cell_shift;
    for (const auto& cell : w.maps[map_id].cells) {
        if (std::abs(cell.x - ccx) > radius_cells || std::abs(cell.z - ccz) > radius_cells) continue;
        for (const auto& o : cell.objects) {
            const mesh_viewer::Model* m = nullptr;
            for (const auto& mm : models)
                for (int t : mm.types)
                    if (t == o.type) m = &mm;
            if (!m || m->lods.empty()) continue;
            int32_t X, Z;
            world::object_units(world::MapId(map_id), cell, o, X, Z);
            const int32_t pos[3] = {X * 256, 0, Z * 256};
            // LOD distance as the game computes it ($30872): hypot of the eye
            // offset in units of 4096 world units, scaled by the zoom ($5ad3a).
            const double dx = (double(pos[0]) - cam[0]) / 4096.0, dy = (double(pos[1]) - cam[1]) / 4096.0,
                         dz = (double(pos[2]) - cam[2]) / 4096.0;
            const double limit = std::sqrt(dx * dx + dy * dy + dz * dz) * gs.camera.zoom / 128.0;
            if (limit >= m->cull_distance) continue;
            const mesh_viewer::Lod* lod = &m->lods[0];
            for (const auto& l : m->lods)
                if (l.distance <= limit) lod = &l;
            for (const auto& p : lod->prims) {
                ViewPrim vp;
                vp.kind = p.kind;
                vp.colour = p.colour;
                vp.mask = p.mask;
                vp.xr = p.xr;
                vp.type = o.type;
                vp.object = next_object_id;
                vp.flat = m->distant || ((lod->flags & 2) && !(lod->flags & 1));
                vp.distance = float(limit);
                for (const auto& mv : p.v) {
                    const int32_t v[3] = {mv.x, mv.y, mv.z};
                    vp.v.push_back(model_to_view(gs.camera.view_m, identity_basis(), pos, cam, v, lod->scale_shift));
                }
                if (visible(vp.v)) out.push_back(std::move(vp));
            }
            ++next_object_id;
        }
    }
    }
    return out;
}

}  // namespace state_scene

namespace state_scene {

std::vector<ViewPrim> build_scenarios(const gamestate::GameState& gs,
                                      const std::vector<mesh_viewer::Model>& models) {
    std::vector<ViewPrim> out;
    const int32_t cam[3] = {gs.camera.eye.x, gs.camera.eye.y, gs.camera.eye.z};
    for (const auto& sc : gs.scenario) {
        if (!sc.active || (sc.index == gs.player && !gs.camera.external)) continue;
        const mesh_viewer::Model* m = nullptr;
        for (const auto& mm : models)
            if (mm.base == sc.model_base && mm.lod_list == sc.lod_list && mm.draw_fn == sc.draw_fn) m = &mm;
        if (!m || m->lods.empty()) continue;
        const int32_t pos[3] = {sc.pos.x, sc.pos.y, sc.pos.z};
        const double dx = (double(pos[0]) - cam[0]) / 4096.0, dy = (double(pos[1]) - cam[1]) / 4096.0,
                     dz = (double(pos[2]) - cam[2]) / 4096.0;
        const double limit = std::sqrt(dx * dx + dy * dy + dz * dz) * gs.camera.zoom / 128.0;
        if (limit >= m->cull_distance) continue;
        const mesh_viewer::Lod* lod = &m->lods[0];
        for (const auto& l : m->lods)
            if (l.distance <= limit) lod = &l;
        for (const auto& p : lod->prims) {
            ViewPrim vp;
            vp.kind = p.kind;
            vp.colour = p.colour;
            vp.mask = p.mask;
            vp.xr = p.xr;
            vp.type = sc.index;
            vp.object = next_object_id;
            vp.flat = m->distant || ((lod->flags & 2) && !(lod->flags & 1));
            vp.distance = float(limit);
            for (const auto& mv : p.v) {
                const int32_t v[3] = {mv.x, mv.y, mv.z};
                vp.v.push_back(model_to_view(gs.camera.view_m, sc.body_m, pos, cam, v, lod->scale_shift));
            }
            if (visible(vp.v)) out.push_back(std::move(vp));
        }
        ++next_object_id;
    }
    return out;
}

}  // namespace state_scene
