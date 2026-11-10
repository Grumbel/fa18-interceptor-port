#include "native/state_scene.hpp"

#include <cstdlib>

namespace state_scene {

std::vector<ViewPrim> build_static(const gamestate::GameState& gs, const world::World& w,
                                   const std::vector<mesh_viewer::Model>& models,
                                   int radius_cells) {
    std::vector<ViewPrim> out;
    if (gs.player < 0 || !w.has_map[world::Near]) return out;
    const int32_t cam[3] = {gs.scenario[gs.player].pos.x, gs.scenario[gs.player].pos.y,
                            gs.scenario[gs.player].pos.z};
    const int ccx = cam[0] >> 22, ccz = cam[2] >> 22;
    for (const auto& cell : w.maps[world::Near].cells) {
        if (std::abs(cell.x - ccx) > radius_cells || std::abs(cell.z - ccz) > radius_cells) continue;
        for (const auto& o : cell.objects) {
            const mesh_viewer::Model* m = nullptr;
            for (const auto& mm : models)
                for (int t : mm.types)
                    if (t == o.type) m = &mm;
            if (!m || m->lods.empty()) continue;
            int32_t X, Z;
            world::object_units(world::Near, cell, o, X, Z);
            const int32_t pos[3] = {X * 256, 0, Z * 256};
            for (const auto& p : m->lods[0].prims) {
                ViewPrim vp;
                vp.kind = p.kind;
                vp.colour = p.colour;
                vp.mask = p.mask;
                vp.xr = p.xr;
                vp.type = o.type;
                bool front = false;
                for (const auto& mv : p.v) {
                    const int32_t v[3] = {mv.x, mv.y, mv.z};
                    Vec3f r = model_to_view(gs.camera.view_m, pos, cam, v);
                    front |= r.z > 0;
                    vp.v.push_back(r);
                }
                if (front) out.push_back(std::move(vp));
            }
        }
    }
    return out;
}

}  // namespace state_scene
