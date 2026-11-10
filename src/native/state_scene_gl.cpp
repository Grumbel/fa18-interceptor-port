// Feeds the GL renderer with primitives generated from the game state
// (--state-scene), instead of the primitives the original rasteriser draws.
#include "native/gl_renderer.hpp"
#include "native/runtime.hpp"
#include "native/state_scene.hpp"

#include <algorithm>
#include <cstdio>

namespace state_scene {
namespace {
std::vector<mesh_viewer::Model> g_models;
world::World g_world;

glr::Ops ops_of(const ViewPrim& p) {
    glr::Ops o;
    o.set = p.colour;
    o.clr = uint8_t(p.mask & ~p.xr);
    o.xr = p.xr;
    return o;
}

void inject() {
    auto rd = [](uint32_t a, int n) -> uint32_t {
        return n == 1 ? native::rd8(a) : n == 2 ? native::rd16(a) : native::rd32(a);
    };
    const gamestate::GameState gs = gamestate::read(rd);
    if (gs.player < 0) return;
    std::vector<ViewPrim> prims = build_static(gs, g_world, g_models, 1);
    for (auto& p : build_scenarios(gs, g_models)) prims.push_back(std::move(p));
    // ground-layer objects first, far to near (painter's order), then solids
    std::stable_sort(prims.begin(), prims.end(), [](const ViewPrim& a, const ViewPrim& b) {
        if (a.flat != b.flat) return a.flat;
        if (a.flat && a.distance != b.distance) return a.distance > b.distance;
        return false;
    });
    int current = -1;
    for (const auto& p : prims) {
        if (p.object != current) {
            current = p.object;
            glr::inject_begin_object(p.flat);
        }
        const glr::Ops o = ops_of(p);
        std::vector<std::array<double, 3>> v;
        for (const auto& q : p.v) v.push_back({q.x, q.y, q.z});
        if (p.kind == mesh_viewer::Prim::Poly) {
            std::vector<double> flat3;
            double buf[32][3];
            const int n = int(std::min<size_t>(v.size(), 32));
            for (int i = 0; i < n; ++i) for (int k = 0; k < 3; ++k) buf[i][k] = v[i][k];
            glr::inject_poly(buf, n, o);
        } else if (p.kind == mesh_viewer::Prim::Line && v.size() >= 2) {
            glr::inject_line(v[0].data(), v[1].data(), o);
        } else if (p.kind == mesh_viewer::Prim::Point && !v.empty()) {
            glr::inject_point(v[0].data(), 0, 0, o);
        }
    }
}
}  // namespace

void install_gl(const std::vector<uint8_t>& exe) {
    g_models = mesh_viewer::load_models(exe);
    std::string err;
    if (!world::decode_exe(exe, g_world, err)) std::fprintf(stderr, "state-scene: world: %s\n", err.c_str());
    glr::set_scene_injector(inject);
}

}  // namespace state_scene
