// Scene capture API: read-only snapshot of the 3D scene spine for remaster
// backends (hi-res software, future OpenGL).  Off by default — enable with
// FA18_SCENE_CAPTURE=1, FA18_SCENE_DUMP=1, or FA18_SCENE_RENDER=<scale>
// (the last also installs SoftwareSceneRenderer and presents via frame_hires).
//
// Design: pure observation.  PC hooks / note_* calls extract state while the
// original (recompiled or interpreted) draw path runs unchanged.  Cycle count,
// interrupts, and game memory are those of the original code only.
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
  uint32_t draw_fn = 0;     // jsr target from type table $36094
  uint32_t aux0 = 0;        // $5ad2e at dispatch
  uint32_t aux1 = 0;        // $5ad32 at dispatch
  uint16_t type_index = 0;  // $5acac — object type / primary mesh identity
  // mesh_id: stable mesh identity for remaster backends.  Currently equals
  // type_index (the $36094 table key).  When render_object selects a concrete
  // drawable/LOD stream, mesh_ptr is also filled from $5ad2a.
  uint16_t mesh_id = 0;
  uint32_t mesh_ptr = 0;    // $5ad2a after LOD selection (0 = not yet rendered)
  uint16_t list_word = 0;   // sorted header word (flags in high bits)
  ObjectList list = ObjectList::Scenario;
  int16_t eye_x = 0, eye_y = 0, eye_z = 0;  // $5ae22..26
  int32_t pos_x = 0, pos_y = 0, pos_z = 0;  // $5ae28..30
  // Approximate camera-space distance from hypot $308cc → $5ae38, filled at
  // object dispatch (before the draw jsr).  Same units as the game's LOD
  // limit local (-40(a6) in $31d6c).
  int16_t distance = 0;
  bool has_render = false;  // true once $31c8c-family body selected a mesh
  uint16_t prim_count = 0;  // primitives attributed to this object this frame
};

enum class PrimKind : uint8_t { Poly3d = 0, Line = 1, Point = 2, Fill2d = 3 };

// Compact view-space (or screen-space for Fill2d) primitive snapshot.
// Fill2d = horizon / sky / ground / sea and other non-mesh screen fills
// (object_index -1).  The software renderer leaves these to the low-res
// composite by default; mesh prims (Poly3d/Line/Point) are re-drawn at scale.
struct Prim {
  PrimKind kind = PrimKind::Poly3d;
  uint8_t n = 0;             // vertex count (capped)
  int8_t shift = 0;          // poly: $61284 shift applied to stored verts
  int16_t sel = 0;           // point: d7 size; Fill2d: 1 = horizon-class
  int16_t object_index = -1; // index into SceneFrame::objects (-1 = non-mesh)
  // Live colour at capture time (ARGB).  0 = unknown → consumer may fall
  // back to mesh_id hash.  Resolved from plane-ops + COLOR00..31 when the
  // original draw hook fires — not a mesh attribute.
  uint32_t rgba = 0;
  int16_t xyz[16][3];        // up to 16 verts; Fill2d uses x,y and z=0
};

struct SceneFrame {
  uint64_t frame_id = 0;
  Camera camera{};
  std::vector<ObjectDraw> objects;
  std::vector<Prim> prims;
  bool complete = false;
};

// Optional consumer for completed frames.  Install with set_renderer();
// called from end_frame() when capture is enabled.  The default is null
// (FA18_SCENE_DUMP still prints to stderr independently).
struct ISceneRenderer {
  virtual ~ISceneRenderer() = default;
  virtual void on_scene(const SceneFrame& frame) = 0;
};

bool enabled();
const SceneFrame& current();

void set_renderer(ISceneRenderer* r);  // not owned; may be null
ISceneRenderer* renderer();

void begin_frame(uint64_t frame_id);
void end_frame();
void note_camera();
void note_object(const ObjectDraw& o);
// Called from the $31c8c-family path after LOD selection: marks the matching
// ObjectDraw as rendered and records mesh_ptr + confirms distance.
void note_render_object(uint32_t mesh_ptr, int16_t distance);
void note_prim(const Prim& p);

}  // namespace scene
