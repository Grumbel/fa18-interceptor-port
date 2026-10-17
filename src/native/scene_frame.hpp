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
  // Scenario: record in $5b47c block. Sorted: pointer from list (may be a
  // type-table / shared header, not always a unique instance record).
  uint32_t obj_base = 0;
  uint32_t list_entry = 0;  // sorted: address of the 24-byte list slot (0 if scen)
  uint32_t draw_fn = 0;     // jsr target
  uint32_t aux0 = 0;        // $5ad2e
  uint32_t aux1 = 0;        // $5ad32
  uint16_t type_index = 0;  // $5acac
  uint16_t list_word = 0;   // sorted header word (flags in high bits)
  ObjectList list = ObjectList::Scenario;
  int16_t eye_x = 0, eye_y = 0, eye_z = 0;  // $5ae22..26
  int32_t pos_x = 0, pos_y = 0, pos_z = 0;  // $5ae28..30
  // Filled when render_object ($31c8c family) runs for this draw (0 = unknown).
  int16_t distance = 0;
  bool has_render = false;
};

enum class PrimKind : uint8_t { Poly3d = 0, Line = 1, Point = 2, Fill2d = 3 };

// Compact view-space (or screen-space for Fill2d) primitive snapshot.
struct Prim {
  PrimKind kind = PrimKind::Poly3d;
  uint8_t n = 0;           // vertex count (capped)
  int8_t shift = 0;        // poly: $61284 shift applied to stored verts
  int16_t sel = 0;         // point: d7 size selector
  int16_t xyz[16][3];      // up to 16 verts; Fill2d uses x,y and z=0
};

struct SceneFrame {
  uint64_t frame_id = 0;
  Camera camera{};
  std::vector<ObjectDraw> objects;
  std::vector<Prim> prims;
  bool complete = false;
};

bool enabled();
const SceneFrame& current();

void begin_frame(uint64_t frame_id);
void end_frame();
void note_camera();
void note_object(const ObjectDraw& o);
void note_render_object(uint32_t obj_hint, int16_t distance);
void note_prim(const Prim& p);

}  // namespace scene
