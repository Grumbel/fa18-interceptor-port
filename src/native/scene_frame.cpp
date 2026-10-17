#include "native/scene_frame.hpp"
#include "amiga/machine.hpp"
#include "native/runtime.hpp"
#include "native/vars.hpp"

#include <cstdio>
#include <cstdlib>

namespace scene {
namespace {

SceneFrame frame;
bool env_checked = false;
bool capture_on = false;
bool dump_on = false;

void check_env() {
  if (env_checked) return;
  env_checked = true;
  dump_on = std::getenv("FA18_SCENE_DUMP") != nullptr;
  capture_on = dump_on || std::getenv("FA18_SCENE_CAPTURE") != nullptr;
}

void dump_frame() {
  if (!dump_on || !frame.complete) return;
  // One line summary + optional detail every N frames
  static uint64_t last_detail = 0;
  std::fprintf(stderr,
               "[scene] frame %llu camera=(%d,%d,%d) objects=%zu\n",
               (unsigned long long)frame.frame_id, frame.camera.x, frame.camera.y,
               frame.camera.z, frame.objects.size());
  if (frame.frame_id >= last_detail + 60) {
    last_detail = frame.frame_id;
    for (size_t i = 0; i < frame.objects.size() && i < 32; ++i) {
      const ObjectDraw& o = frame.objects[i];
      std::fprintf(stderr,
                   "  [%zu] list=%s base=%06x type=%u fn=%06x eye=(%d,%d,%d) "
                   "pos=(%d,%d,%d)\n",
                   i, o.list == ObjectList::Scenario ? "scen" : "sort", o.obj_base,
                   unsigned(o.type_index), o.draw_fn, int(o.eye_x), int(o.eye_y),
                   int(o.eye_z), o.pos_x, o.pos_y, o.pos_z);
    }
    if (frame.objects.size() > 32)
      std::fprintf(stderr, "  ... +%zu more\n", frame.objects.size() - 32);
  }
}

}  // namespace

bool enabled() {
  check_env();
  return capture_on;
}

const SceneFrame& current() { return frame; }

void begin_frame(uint64_t frame_id) {
  if (!enabled()) return;
  frame = SceneFrame{};
  frame.frame_id = frame_id;
  frame.complete = false;
  frame.objects.reserve(64);
}

void end_frame() {
  if (!enabled()) return;
  frame.complete = true;
  dump_frame();
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
}

}  // namespace scene
