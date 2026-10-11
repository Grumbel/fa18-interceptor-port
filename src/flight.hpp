#pragma once
#include <vector>
#include "transform.hpp"
#include "draw.hpp"
#include "gfx_state.hpp"

namespace fa18 {
// Object pool mirrors recovered layout:
//   base $642, stride 64 bytes, max 20 objects, current ptr at $c
constexpr int kObjectStride = 64;
constexpr int kMaxObjects   = 20;


// Mirrors the recovered flight-frame structure:
//   frame $1124 → object_loop $72dc → transform $77d8 → world store → draw
// Object layout: docs/object_list.md

struct Object {
  // +0 / +4 / +8 – world position after transform + view
  Vec3 world{};

  // Local model space
  Vec3 pos{};            // local origin before transform
  Mat3 orient{};         // float path (demos)
  Mat3Fixed orient_fp{}; // Q2.14 path matching $be6

  std::vector<Vec3> local_verts;
  std::vector<std::pair<int,int>> edges;

  uint32_t flags = 0;    // +16 ($10)
  uint16_t state = 0;    // +40 ($28)
  int16_t  counter_a = 0; // +48 ($30), clamp 0..$7f
  int16_t  counter_b = 0; // +50 ($32)
};

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

// One iteration of the object loop ($72dc):
//   transform → apply view translation → project → draw
inline void draw_object(SDL_Renderer* ren, GfxState& gfx, Object& obj) {
  std::vector<int> sx(obj.local_verts.size());
  std::vector<int> sy(obj.local_verts.size());
  for (size_t i = 0; i < obj.local_verts.size(); ++i) {
    Vec3 w = transform_point(obj.orient, obj.local_verts[i]);
    w.x += obj.pos.x;
    w.y += obj.pos.y;
    w.z += obj.pos.z;
    w = apply_view(w, gfx.view_x, gfx.view_y, gfx.view_z);
    if (i == 0) obj.world = w;
    project(w, sx[i], sy[i]);
  }
  for (auto [a, b] : obj.edges) {
    blitter_line_stub(ren, gfx, sx[a], sy[a], sx[b], sy[b]);
  }
}


// Fixed-point path mirroring transform $77d8 + project 0x2864a
inline void draw_object_fp(SDL_Renderer* ren, GfxState& gfx, Object& obj) {
  std::vector<int> sx(obj.local_verts.size());
  std::vector<int> sy(obj.local_verts.size());
  for (size_t i = 0; i < obj.local_verts.size(); ++i) {
    const auto& lv = obj.local_verts[i];
    // Scale local verts into a range the >>6 transform expects
    int16_t lx = static_cast<int16_t>(lv.x * 64.f);
    int16_t ly = static_cast<int16_t>(lv.y * 64.f);
    int16_t lz = static_cast<int16_t>(lv.z * 64.f);
    Vec3 w = transform_point_fp(obj.orient_fp, lx, ly, lz);
    w.x += obj.pos.x;
    w.y += obj.pos.y;
    w.z += obj.pos.z;
    w = apply_view(w, gfx.view_x, gfx.view_y, gfx.view_z);
    if (i == 0) obj.world = w;
    project(w, sx[i], sy[i]);
  }
  for (auto [a, b] : obj.edges) {
    blitter_line_stub(ren, gfx, sx[a], sy[a], sx[b], sy[b]);
  }
}

} // namespace fa18
