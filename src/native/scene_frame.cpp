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
               "[scene] frame %llu camera=(%d,%d,%d) objects=%zu prims=%zu\n",
               (unsigned long long)frame.frame_id, frame.camera.x, frame.camera.y,
               frame.camera.z, frame.objects.size(), frame.prims.size());
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
      std::fprintf(stderr, "  prim %s n=%u obj=%d", kind_name(p.kind), unsigned(p.n),
                   int(p.object_index));
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

void begin_frame(uint64_t frame_id) {
  if (!enabled()) return;
  frame = SceneFrame{};
  frame.frame_id = frame_id;
  current_object_ = -1;
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
