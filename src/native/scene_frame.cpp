#include "native/scene_frame.hpp"
#include "native/scene_renderer.hpp"
#include "amiga/machine.hpp"
#include "native/runtime.hpp"

#include <cstdio>
#include <cstdlib>

namespace scene {
namespace {

SceneFrame frame;
bool env_checked = false;
bool capture_on = false;
bool dump_on = false;
ISceneRenderer* renderer_ = nullptr;
// Index of the object most recently noted; prims emitted until the next
// note_object are attributed to it.  -1 = none yet this frame.
int current_object_ = -1;

void check_env() {
  if (env_checked) return;
  env_checked = true;
  dump_on = std::getenv("FA18_SCENE_DUMP") != nullptr;
  // FA18_SCENE_RENDER implies capture so the software renderer receives frames.
  capture_on = dump_on || std::getenv("FA18_SCENE_CAPTURE") != nullptr
               || std::getenv("FA18_SCENE_RENDER") != nullptr;
  ensure_software_renderer();
}

const char* kind_name(PrimKind k) {
  switch (k) {
    case PrimKind::Poly3d: return "poly";
    case PrimKind::Line: return "line";
    case PrimKind::Point: return "point";
    case PrimKind::Fill2d: return "fill2d";
  }
  return "?";
}

void dump_frame() {
  if (!dump_on || !frame.complete) return;
  static uint64_t last_detail = 0;
  std::fprintf(stderr,
               "[scene] frame %llu camera=(%d,%d,%d) objects=%zu prims=%zu "
               "sky=%06x ground=%06x\n",
               (unsigned long long)frame.frame_id, frame.camera.x, frame.camera.y,
               frame.camera.z, frame.objects.size(), frame.prims.size(),
               unsigned(frame.sky_rgba & 0xFFFFFF),
               unsigned(frame.ground_rgba & 0xFFFFFF));
  if (frame.frame_id >= last_detail + 60) {
    last_detail = frame.frame_id;
    for (size_t i = 0; i < frame.objects.size() && i < 24; ++i) {
      const ObjectDraw& o = frame.objects[i];
      std::fprintf(stderr,
                   "  obj[%zu] %s base=%06x type=%u mesh=%u ptr=%06x "
                   "dist=%d prims=%u%s\n",
                   i, o.list == ObjectList::Scenario ? "scen" : "sort", o.obj_base,
                   unsigned(o.type_index), unsigned(o.mesh_id), o.mesh_ptr,
                   int(o.distance), unsigned(o.prim_count),
                   o.has_render ? "" : " (no render)");
    }
    if (frame.objects.size() > 24)
      std::fprintf(stderr, "  ... +%zu objects\n", frame.objects.size() - 24);
    size_t np = 0;
    for (const Prim& p : frame.prims) {
      if (np++ >= 12) break;
      std::fprintf(stderr, "  prim %s n=%u obj=%d sel=%d", kind_name(p.kind), unsigned(p.n),
                   int(p.object_index), int(p.sel));
      if (p.rgba)
        std::fprintf(stderr, " rgba=%06x", unsigned(p.rgba & 0xFFFFFF));
      if (p.n > 0)
        std::fprintf(stderr, " v0=(%d,%d,%d)", int(p.xyz[0][0]), int(p.xyz[0][1]),
                     int(p.xyz[0][2]));
      std::fprintf(stderr, "\n");
    }
    if (frame.prims.size() > 12)
      std::fprintf(stderr, "  ... +%zu prims\n", frame.prims.size() - 12);
  }
}

}  // namespace

bool enabled() {
  check_env();
  return capture_on;
}

const SceneFrame& current() { return frame; }

void set_renderer(ISceneRenderer* r) { renderer_ = r; }
ISceneRenderer* renderer() { return renderer_; }

uint32_t rgba_from_color_reg(int idx) {
  if (!native::g_machine) return 0;
  uint16_t c12 = native::g_machine->color_reg(idx & 31);
  uint32_t r = (c12 >> 8) & 0xF, g = (c12 >> 4) & 0xF, b = c12 & 0xF;
  r |= r << 4; g |= g << 4; b |= b << 4;
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}

void begin_frame(uint64_t frame_id) {
  if (!enabled()) return;
  // Idempotent: game_frame opens the frame early (horizon/sky); $2fc14
  // must not wipe prims already captured this frame.
  if (frame.frame_id == frame_id && !frame.complete) return;
  frame = SceneFrame{};
  frame.frame_id = frame_id;
  current_object_ = -1;
  // Default environment colours from the live playfield palette.
  // COLOR0 is often 0 (black) in this game — the sky is a filled plane, not
  // the backdrop.  Fall back to a mid blue so a missing fill is obvious but
  // not a void.
  frame.sky_rgba = rgba_from_color_reg(0);
  if ((frame.sky_rgba & 0xFFFFFF) == 0)
    frame.sky_rgba = 0xFF5080C0u;
  frame.ground_rgba = rgba_from_color_reg(2);
  {
    uint32_t sky = frame.sky_rgba;
    int best_score = -1;
    for (int idx : {2, 3, 6, 7, 4, 5, 1, 8, 9, 10, 11, 12, 13, 14, 15}) {
      uint32_t c = rgba_from_color_reg(idx);
      if ((c & 0xFFFFFF) == 0) continue;  // skip black
      int dr = int((c >> 16) & 255) - int((sky >> 16) & 255);
      int dg = int((c >> 8) & 255) - int((sky >> 8) & 255);
      int db = int(c & 255) - int(sky & 255);
      int score = dr * dr + dg * dg + db * db;
      int blue_bias = int(c & 255) - int((c >> 16) & 255);
      if (blue_bias > 0) score += blue_bias * blue_bias;
      if (score > best_score) {
        best_score = score;
        frame.ground_rgba = c;
      }
    }
    if ((frame.ground_rgba & 0xFFFFFF) == 0)
      frame.ground_rgba = 0xFF206080u;  // fallback water
  }
}

void end_frame() {
  if (!enabled()) return;
  frame.complete = true;
  dump_frame();
  if (renderer_) renderer_->on_scene(frame);
}

void note_camera() {
  if (!enabled()) return;
  frame.camera.x = int32_t(native::rd32(0x5ad5a));
  frame.camera.y = int32_t(native::rd32(0x5ad5e));
  frame.camera.z = int32_t(native::rd32(0x5ad62));
}

void note_env_colours(uint32_t sky_rgba, uint32_t ground_rgba) {
  if (!enabled()) return;
  if (sky_rgba) frame.sky_rgba = sky_rgba;
  if (ground_rgba) frame.ground_rgba = ground_rgba;
}

void note_object(const ObjectDraw& o) {
  if (!enabled()) return;
  frame.objects.push_back(o);
  current_object_ = int(frame.objects.size()) - 1;
}

void note_render_object(uint32_t mesh_ptr, int16_t distance) {
  if (!enabled() || frame.objects.empty()) return;
  // Prefer matching the last object without has_render; else last overall.
  for (auto it = frame.objects.rbegin(); it != frame.objects.rend(); ++it) {
    ObjectDraw& o = *it;
    if (o.has_render) continue;
    o.distance = distance;
    o.mesh_ptr = mesh_ptr;
    if (mesh_ptr) o.mesh_id = o.type_index;
    o.has_render = true;
    return;
  }
  ObjectDraw& o = frame.objects.back();
  o.distance = distance;
  o.mesh_ptr = mesh_ptr;
  if (mesh_ptr) o.mesh_id = o.type_index;
  o.has_render = true;
}

void note_prim(const Prim& p) {
  if (!enabled()) return;
  Prim out = p;
  if (out.object_index < 0) out.object_index = int16_t(current_object_);
  if (out.object_index >= 0 && size_t(out.object_index) < frame.objects.size()) {
    ObjectDraw& o = frame.objects[size_t(out.object_index)];
    if (o.prim_count < 0xffff) ++o.prim_count;
  }
  frame.prims.push_back(out);
}

}  // namespace scene
