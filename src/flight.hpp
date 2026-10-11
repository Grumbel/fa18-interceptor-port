#pragma once
#include <vector>
#include <cstdint>
#include "transform.hpp"
#include "draw.hpp"
#include "gfx_state.hpp"

namespace fa18 {

// Object pool: base $642, stride 64 bytes, max 20, current ptr at $c
constexpr int kObjectStride = 64;
constexpr int kMaxObjects   = 20;
constexpr int32_t kViewMask = 0x3fffff;  // applied to view X and Z

// Mirrors recovered flight-frame:
//   frame $1124 → object_loop $72dc → transform $77d8 → world store → draw
// Object layout: docs/object_list.md

struct Object {
  // +0 / +4 / +8 – world position after transform + view
  Vec3 world{};

  // Local model space (port-side convenience)
  Vec3 pos{};
  Mat3 orient{};
  Mat3Fixed orient_fp{};

  std::vector<Vec3> local_verts;
  std::vector<std::pair<int,int>> edges;

  uint32_t flags = 0;          // +16 ($10)
  int16_t  sin_q14 = 0;        // +34 ($22)
  int16_t  cos_q14 = 0x4000;   // +36 ($24)
  uint16_t type_flags = 0;     // +38 ($26)
  uint16_t state = 0;          // +40 ($28)
  int16_t  counter_a = 0;      // +48 ($30)
  int16_t  counter_b = 0;      // +50 ($32)
};

// Exact view translation from transform 0x7ad8 world-store:
//   obj.x = transformed_x + (view_x & $3fffff)
//   obj.y = transformed_y +  view_y
//   obj.z = transformed_z + (view_z & $3fffff)
inline Vec3 apply_view_masked(const Vec3& v, const GfxState& gfx) {
  return {
    v.x + float(gfx.view_x & kViewMask),
    v.y + float(gfx.view_y),
    v.z + float(gfx.view_z & kViewMask),
  };
}

inline Object make_cube(float scale = 1.f) {
  Object o;
  o.pos = {0, 0, 0};
  o.orient = mat_yaw(0);
  o.orient_fp = mat_from_angles_q14(0, 0, 0);
  o.local_verts = {
    {-scale,-scale,-scale},{ scale,-scale,-scale},
    { scale, scale,-scale},{-scale, scale,-scale},
    {-scale,-scale, scale},{ scale,-scale, scale},
    { scale, scale, scale},{-scale, scale, scale},
  };
  o.edges = {
    {0,1},{1,2},{2,3},{3,0},
    {4,5},{5,6},{6,7},{7,4},
    {0,4},{1,5},{2,6},{3,7},
  };
  return o;
}

// Float path (demos)
inline void draw_object(SDL_Renderer* ren, GfxState& gfx, Object& obj) {
  std::vector<int> sx(obj.local_verts.size());
  std::vector<int> sy(obj.local_verts.size());
  for (size_t i = 0; i < obj.local_verts.size(); ++i) {
    Vec3 w = transform_point(obj.orient, obj.local_verts[i]);
    w.x += obj.pos.x;
    w.y += obj.pos.y;
    w.z += obj.pos.z;
    w = apply_view_masked(w, gfx);
    if (i == 0) obj.world = w;
    project(w, sx[i], sy[i]);
  }
  for (auto [a, b] : obj.edges) {
    blitter_line_stub(ren, gfx, sx[a], sy[a], sx[b], sy[b]);
  }
}

// Fixed-point path: transform $77d8 (>>6) + world store + project 0x2864a
inline void draw_object_fp(SDL_Renderer* ren, GfxState& gfx, Object& obj) {
  std::vector<int> sx(obj.local_verts.size());
  std::vector<int> sy(obj.local_verts.size());
  for (size_t i = 0; i < obj.local_verts.size(); ++i) {
    const auto& lv = obj.local_verts[i];
    int16_t lx = static_cast<int16_t>(lv.x * 64.f);
    int16_t ly = static_cast<int16_t>(lv.y * 64.f);
    int16_t lz = static_cast<int16_t>(lv.z * 64.f);
    Vec3 w = transform_point_fp(obj.orient_fp, lx, ly, lz);
    w.x += obj.pos.x;
    w.y += obj.pos.y;
    w.z += obj.pos.z;
    w = apply_view_masked(w, gfx);
    if (i == 0) obj.world = w;
    project(w, sx[i], sy[i]);
  }
  for (auto [a, b] : obj.edges) {
    blitter_line_stub(ren, gfx, sx[a], sy[a], sx[b], sy[b]);
  }
  obj.state = 2;  // post-transform sets state = 2
}

// Minimal object list mirroring $b8f count + $642 pool
struct ObjectList {
  Object objects[kMaxObjects];
  int count = 0;  // $b8f

  void clear() { count = 0; }
  Object* add() {
    if (count >= kMaxObjects) return nullptr;
    return &objects[count++];
  }
  // One frame of object_loop $72dc
  void draw_all_fp(SDL_Renderer* ren, GfxState& gfx) {
    for (int i = 0; i < count; ++i)
      draw_object_fp(ren, gfx, objects[i]);
  }
};

} // namespace fa18
