// Scene capture API: read-only snapshot of the 3D scene spine for remaster
// backends (hi-res software, future OpenGL).  Off by default — enable with
// FA18_SCENE_CAPTURE=1 (or FA18_SCENE_DUMP=1, which implies capture).
// Does not alter game memory or control flow.
#pragma once

#include <cstdint>
#include <vector>

namespace scene {

struct Camera {
  int32_t x = 0, y = 0, z = 0;  // $5ad5a / $5ad5e / $5ad62 after $2f220
};

enum class ObjectList : uint8_t { Scenario = 0, Sorted = 1 };

struct ObjectDraw {
  uint32_t obj_base = 0;    // object record (scenario base + index, or list a1)
  uint32_t draw_fn = 0;     // type-table entry called (A2)
  uint32_t aux0 = 0;        // $5ad2e from type table
  uint32_t aux1 = 0;        // $5ad32
  uint16_t type_index = 0;  // $5acac
  uint16_t list_word = 0;   // sorted-list header word (flags in high bits)
  ObjectList list = ObjectList::Scenario;
  int16_t eye_x = 0, eye_y = 0, eye_z = 0;  // $5ae22..26 view-ish words
  int32_t pos_x = 0, pos_y = 0, pos_z = 0;  // $5ae28..30 before draw
};

struct SceneFrame {
  uint64_t frame_id = 0;
  Camera camera{};
  std::vector<ObjectDraw> objects;
  bool complete = false;
};

// True when capture is active this process (env checked once).
bool enabled();

const SceneFrame& current();

void begin_frame(uint64_t frame_id);
void end_frame();
void note_camera();
void note_object(const ObjectDraw& o);

}  // namespace scene
