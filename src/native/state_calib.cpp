// Probe: compare state-generated static prims with captured prims
// (FA18_STATE_CALIB=frame).  Temporary development aid.
#include "native/game_state.hpp"
#include "native/mesh_viewer.hpp"
#include "native/runtime.hpp"
#include "native/scene_frame.hpp"
#include "native/state_scene.hpp"
#include "native/world.hpp"

#include <cstdio>
#include <cstdlib>
#include <map>

namespace {
std::vector<mesh_viewer::Model> g_models;
world::World g_world;
struct Probe : scene::ISceneRenderer {
  uint64_t frame_want = 0;
  void on_scene(const scene::SceneFrame& f) override {
    static bool done = false;
    if (done || f.frame_id < frame_want) return;
    done = true;
    auto rd = [](uint32_t a, int n) -> uint32_t {
      return n == 1 ? native::rd8(a) : n == 2 ? native::rd16(a) : native::rd32(a);
    };
    gamestate::GameState gs = gamestate::read(rd);
    auto gen = state_scene::build_static(gs, g_world, g_models, 1);
    std::map<int, int> gen_by_type, cap_by_obj;
    for (auto& p : gen) if (p.kind == mesh_viewer::Prim::Poly) gen_by_type[p.type]++;
    for (auto& p : f.prims) if (p.kind == scene::PrimKind::Poly3d) cap_by_obj[p.object_index]++;
    std::fprintf(stderr, "CALIB gen polys by type:");
    for (auto& [t, n] : gen_by_type) std::fprintf(stderr, " %d:%d", t, n);
    std::fprintf(stderr, "\nCALIB captured objects:");
    for (size_t i = 0; i < f.objects.size(); ++i)
      std::fprintf(stderr, " [type %u prims %u]", f.objects[i].type_index, f.objects[i].prim_count);
    std::fprintf(stderr, "\n");
    // match each captured poly vertex 0 against generated ones
    int hit = 0, total = 0, unm = 0;
    for (auto& p : f.prims) {
      if (p.kind != scene::PrimKind::Poly3d || p.n < 3) continue;
      ++total;
      bool matched = false;
      for (auto& g : gen) {
        if (g.kind != mesh_viewer::Prim::Poly || g.v.size() != size_t(p.n)) continue;
        bool ok = true;
        for (int k = 0; k < p.n && ok; ++k)
          ok = std::abs(g.v[k].x - p.xyz[k][0]) < 12 && std::abs(g.v[k].y - p.xyz[k][1]) < 12 &&
               std::abs(g.v[k].z - p.xyz[k][2]) < 24;
        if (ok) { ++hit; matched = true; break; }
      }
      if (!matched && unm++ < 12) std::fprintf(stderr, "  unmatched obj %d n %d shift %d (%d,%d,%d)\n", p.object_index, p.n, p.shift, p.xyz[0][0], p.xyz[0][1], p.xyz[0][2]);
    }
    std::fprintf(stderr, "CALIB captured polys %d matched by state-generated %d\n", total, hit);
  }
} g_probe;
}  // namespace

void state_calib_install(const std::vector<uint8_t>& exe) {
  const char* e = std::getenv("FA18_STATE_CALIB");
  if (!e) return;
  g_models = mesh_viewer::load_models(exe);
  std::string err;
  if (!world::decode_exe(exe, g_world, err)) std::fprintf(stderr, "world: %s\n", err.c_str());
  g_probe.frame_want = std::strtoull(e, nullptr, 10);
  scene::set_renderer(&g_probe);
}
