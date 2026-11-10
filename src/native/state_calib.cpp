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
#include <algorithm>
#include <cmath>
#include <map>

namespace {
static float TOL = std::getenv("FA18_TOL") ? float(std::atof(std::getenv("FA18_TOL"))) : 3.f;
std::vector<mesh_viewer::Model> g_models;
world::World g_world;
struct Probe : scene::ISceneRenderer {
  uint64_t frame_want = 0;
  void on_scene(const scene::SceneFrame& f) override {
    static bool done = false;
    if (done || f.frame_id < frame_want) return;
    if (std::getenv("FA18_STATE_CALIB_SCEN")) {
      bool any = false;
      for (auto& o : f.objects) any |= o.type_index != 0 && o.type_index < 16 && o.prim_count > 0;
      if (!any) return;
    }
    done = true;
    std::fprintf(stderr, "CALIB at frame %llu\n", (unsigned long long)f.frame_id);
    auto rd = [](uint32_t a, int n) -> uint32_t {
      return n == 1 ? native::rd8(a) : n == 2 ? native::rd16(a) : native::rd32(a);
    };
    gamestate::GameState gs = gamestate::read(rd);
    if (std::getenv("FA18_STATE_CALIB_SCEN")) state_scene::set_frustum_cull(false);
    auto gen = state_scene::build_static(gs, g_world, g_models, 1);
    for (auto& sp : state_scene::build_scenarios(gs, g_models)) gen.push_back(sp);
    if (std::getenv("FA18_STATE_CALIB_SCEN")) {
      const auto& sc = gs.scenario[1];
      std::fprintf(stderr, "CALIB sc1 active %d draw %x base %x lodl %x\n", sc.active, sc.draw_fn, sc.model_base, sc.lod_list);
      for (auto& m : g_models) std::fprintf(stderr, "CALIB   model draw %x base %x lodl %x lods %zu\n", m.draw_fn, m.base, m.lod_list, m.lods.size());
    }
    std::map<int, int> gen_by_type, cap_by_obj;
    for (auto& p : gen) if (p.kind == mesh_viewer::Prim::Poly) gen_by_type[p.type]++;
    for (auto& p : f.prims) if (p.kind == scene::PrimKind::Poly3d) cap_by_obj[p.object_index]++;
    std::fprintf(stderr, "CALIB gen polys by type:");
    for (auto& [t, n] : gen_by_type) std::fprintf(stderr, " %d:%d", t, n);
    std::fprintf(stderr, "\nCALIB captured objects:");
    for (size_t i = 0; i < f.objects.size(); ++i)
      std::fprintf(stderr, " [type %u prims %u]", f.objects[i].type_index, f.objects[i].prim_count);
    std::fprintf(stderr, "\n");
    if (std::getenv("FA18_STATE_CALIB_SCEN")) {
      for (size_t i = 0; i < f.objects.size(); ++i) {
        const auto& o = f.objects[i];
        if (o.type_index == 0 || o.type_index >= 16 || !o.prim_count) continue;
        const auto& sc = gs.scenario[o.type_index];
        const mesh_viewer::Lod* lod = nullptr;
        for (auto& m : g_models) for (auto& l : m.lods) if (l.mesh == o.mesh_ptr) lod = &l;
        std::fprintf(stderr, "CALIB scen obj %zu type %u mesh %06x lod %s dist %d\n", i, o.type_index, o.mesh_ptr, lod ? "found" : "NOT FOUND", o.distance);
        std::fprintf(stderr, "CALIB   obj pos %d %d %d eye %d %d %d\n", o.pos_x, o.pos_y, o.pos_z, o.eye_x, o.eye_y, o.eye_z);
        { int q = 0; for (auto& cp : f.prims) if (cp.object_index == int(i) && q++ < 4) { std::fprintf(stderr, "CALIB   cap k%d n%d:", int(cp.kind), cp.n); for (int k = 0; k < cp.n && k < 4; ++k) std::fprintf(stderr, " (%d,%d,%d)", cp.xyz[k][0], cp.xyz[k][1], cp.xyz[k][2]); std::fprintf(stderr, "\n"); } }
        { int q = 0; for (auto& mp : lod->prims) if (q++ < 4) { std::fprintf(stderr, "CALIB   mod k%d n%zu:", int(mp.kind), mp.v.size()); for (size_t k = 0; k < mp.v.size() && k < 4; ++k) std::fprintf(stderr, " (%d,%d,%d)", mp.v[k].x, mp.v[k].y, mp.v[k].z); std::fprintf(stderr, "\n"); } }
        if (!lod) continue;
        const int32_t cam[3] = {-f.camera.x, -f.camera.y, -f.camera.z};
        const int32_t opos[3] = {o.pos_x, o.pos_y, o.pos_z};
        for (int tr = 0; tr < 2; ++tr)
          for (float scale : {64.f}) {
            double sumerr = 0; int cnt = 0;
            for (auto& cp : f.prims) {
              if (cp.object_index != int(i) || cp.kind != scene::PrimKind::Poly3d) continue;
              double best = 1e18;
              for (auto& mp : lod->prims) {
                if (mp.kind != mesh_viewer::Prim::Poly || mp.v.size() != size_t(cp.n)) continue;
                double e = 0;
                for (int k = 0; k < cp.n; ++k) {
                  double v[3] = {double(mp.v[k].x), double(mp.v[k].y), double(mp.v[k].z)}, r[3];
                  for (int a = 0; a < 3; ++a) {
                    r[a] = 0;
                    for (int b = 0; b < 3; ++b) r[a] += v[b] * sc.body_m[tr ? b * 3 + a : a * 3 + b] / 16384.0;
                  }
                  int32_t iv[3] = {int32_t(std::lround(r[0])), int32_t(std::lround(r[1])), int32_t(std::lround(r[2]))};
                  auto o2 = state_scene::model_to_view(gs.camera.view_m, opos, cam, iv, scale);
                  e += std::abs(64 * o2.x - cp.xyz[k][0]) + std::abs(64 * o2.y - cp.xyz[k][1]) + std::abs(64 * o2.z - cp.xyz[k][2]);
                }
                best = std::min(best, e / cp.n);
              }
              if (best < 1e17) { sumerr += best; ++cnt; }
            }
            std::fprintf(stderr, "CALIB   transpose %d scale %g: mean err %.1f over %d polys\n", tr, scale, cnt ? sumerr / cnt : -1, cnt);
          }
      }
    }
    for (uint32_t m : {0x050f72u, 0x04e1a0u, 0x04fa98u}) {
      std::fprintf(stderr, "CALIB hdr %06x:", m);
      for (int k = 0; k < 12; ++k) std::fprintf(stderr, " %02x", native::rd8(m + k));
      std::fprintf(stderr, "\n");
    }
    // match each captured poly vertex 0 against generated ones
    int hit = 0, total = 0, unm = 0;
    for (auto& p : f.prims) {
      if (p.kind != scene::PrimKind::Poly3d || p.n < 3) continue;
      ++total;
      bool matched = false;
      for (auto& g : gen) {
        if (g.kind != mesh_viewer::Prim::Poly || g.v.size() != size_t(p.n)) continue;
        bool ok = false;
        for (float fac : {1.f, 2.f, 4.f, 8.f, 16.f, 32.f, 64.f, 128.f, 256.f}) {   // game vertex-stage unit differs per object
          bool o2 = true;
          for (int k = 0; k < p.n && o2; ++k)
            o2 = std::abs(fac * g.v[k].x - p.xyz[k][0]) < TOL && std::abs(fac * g.v[k].y - p.xyz[k][1]) < TOL &&
                 std::abs(fac * g.v[k].z - p.xyz[k][2]) < TOL;
          ok |= o2;
        }
        if (ok) { ++hit; matched = true; break; }
      }
      if (!matched && (unm++ < 12 || p.object_index >= 0)) std::fprintf(stderr, "  unmatched obj %d n %d shift %d (%d,%d,%d)\n", p.object_index, p.n, p.shift, p.xyz[0][0], p.xyz[0][1], p.xyz[0][2]);
    }
    for (auto& g : gen) if (g.type == 1 && g.kind == mesh_viewer::Prim::Poly) { std::fprintf(stderr, "  gen sc1 n %zu:", g.v.size()); for (auto& v : g.v) std::fprintf(stderr, " (%.1f,%.1f,%.1f)", 64*v.x, 64*v.y, 64*v.z); std::fprintf(stderr, "\n"); }
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
