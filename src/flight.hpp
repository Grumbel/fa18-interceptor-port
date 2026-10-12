#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
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
  // +0 / +4 / +8 – world position (integrated by $c6 motion step)
  Vec3 world{};

  // Local model space (port-side convenience)
  Vec3 pos{};
  Mat3 orient{};
  Mat3Fixed orient_fp{};

  std::vector<Vec3> local_verts;
  std::vector<std::pair<int,int>> edges;

  // Velocity tiers recovered from $c6 (docs/func_c6.md):
  //   +12..16 = tier A (far / accept path)
  //   +24..28 = tier B (near path)
  // Stored as float for the port; original is 16-bit words with asr#8 scaling.
  Vec3 vel_far{};   // object+12
  Vec3 vel_near{};  // object+24

  uint32_t flags = 0;          // +16 ($10)
  int16_t  sin_q14 = 0;        // +34 ($22)
  int16_t  cos_q14 = 0x4000;   // +36 ($24)  also used as active marker in $c6
  uint16_t type_flags = 0;     // +38 ($26)  bits 4/5 select damping
  uint16_t state = 0;          // +40 ($28)
  int16_t  counter_a = 0;      // +48 ($30)
  int16_t  counter_b = 0;      // +50 ($32)
};

// View/camera descriptor at $b54 (live $00C44EBC) — fields sampled by $c6
struct ViewDesc {
  float axis0 = 0;   // $b54+12 → -26(a6)
  float axis1 = 0;   // $b54+16 → -22(a6)
  float axis2 = 0;   // $b54+14 → -20(a6)
  int   threshold_coarse = 0x40;  // $376
  int   threshold_fine   = 0x20;  // $378
};

// Player attitude state at live $00C44EBC (docs/transform_3d.md).
// Angles in 7200-circle units (7200 = 360°).
constexpr int kAngleCircle = 7200;

struct PlayerAttitude {
  // Raw rates from stick curves (+$50/+$52/+$54)
  int16_t raw_rate[3] = {};
  // Filtered rates after scale+blend (+$56/+$58/+$5A)
  int16_t rate[3] = {};
  // Attitude angles (+$68/+$6A/+$6C)
  int16_t angle[3] = {};
  // Mode nibble selects curve (+$62)
  uint8_t mode = 0x10;
  // Scale divisor for rate filter (5..15)
  int16_t scale = 10;
};

// Stick curve tables (live $00C2B782 / $00C2B798 / $00C2B7C4)
inline const int16_t kCurveSteep[]  = {
  160,320,480,560,640,720,800,960,1040,1120,1280};
inline const int16_t kCurveMedium[] = {
  80,120,160,240,320,320,400,480,480,560,640};
inline const int16_t kCurveGentle[] = {
  8,16,24,32,40,48,56,64,72,80,88};

inline const int16_t* select_curve(uint8_t mode) {
  if ((mode & 0xF0) == 0x10) return kCurveSteep;
  if (mode == 0x14) return kCurveMedium;
  return kCurveGentle;
}

// Low-pass rate filter ($00C2C1F0): scale new deltas, blend 50/50 with old.
inline void filter_rates(PlayerAttitude& att, const int16_t new_delta[3]) {
  int16_t sc = att.scale;
  if (sc < 5) sc = 5;
  if (sc > 15) sc = 15;
  for (int i = 0; i < 3; ++i) {
    int16_t nd = static_cast<int16_t>(new_delta[i] / sc);
    int16_t avg = static_cast<int16_t>((att.rate[i] + nd) / 2);
    int16_t smoothed = static_cast<int16_t>((avg + nd) / 2);
    att.rate[i] = smoothed;
  }
}

// Integrator ($00C2C418): angle0 -= rate0, angle1 -= rate1.
inline void integrate_attitude(PlayerAttitude& att) {
  att.angle[0] = static_cast<int16_t>(att.angle[0] - att.rate[0]);
  att.angle[1] = static_cast<int16_t>(att.angle[1] - att.rate[1]);
  // Wrap into [0, kAngleCircle)
  for (int i = 0; i < 2; ++i) {
    while (att.angle[i] < 0) att.angle[i] =
        static_cast<int16_t>(att.angle[i] + kAngleCircle);
    while (att.angle[i] >= kAngleCircle) att.angle[i] =
        static_cast<int16_t>(att.angle[i] - kAngleCircle);
  }
}

// Rate auto-center ($00C2C476): nudge filtered rates toward zero.
// Live global $00C07384 = 144.
inline void auto_center_rates(PlayerAttitude& att, int16_t strength = 144) {
  for (int i = 0; i < 2; ++i) {
    int16_t bias = (att.rate[i] >= 0) ? strength : static_cast<int16_t>(-strength);
    // Original adds bias into the rate long; approximate with saturating add.
    int32_t next = int32_t(att.rate[i]) + bias;
    // Don't cross through zero in one step — spring toward zero.
    if ((att.rate[i] > 0 && next < 0) || (att.rate[i] < 0 && next > 0))
      att.rate[i] = 0;
    else if (next > 32767) att.rate[i] = 32767;
    else if (next < -32768) att.rate[i] = -32768;
    else att.rate[i] = static_cast<int16_t>(next);
  }
}

// JOY0DAT stick path (docs/transform_3d.md).
// Raw bytes from $DFF00A → accumulators $00C44AE (X) / $00C44B0 (Y).
struct JoyAxes {
  int16_t raw_x = 0;    // $00C1975E
  int16_t raw_y = 0;    // $00C19760
  int16_t accum_x = 0;  // $00C44AE
  int16_t accum_y = 0;  // $00C44B0
};

inline void joy_update(JoyAxes& joy, uint16_t joy0dat) {
  int16_t x = static_cast<int16_t>(joy0dat & 0xFF);
  int16_t y = static_cast<int16_t>((joy0dat >> 8) & 0xFF);
  int16_t dx = static_cast<int16_t>(x - joy.raw_x);
  int16_t dy = static_cast<int16_t>(y - joy.raw_y);
  // Clamp deltas to ±0x7F
  if (dx < -0x7F) dx = -0x7F;
  if (dx >  0x7F) dx =  0x7F;
  if (dy < -0x7F) dy = -0x7F;
  if (dy >  0x7F) dy =  0x7F;
  joy.accum_x = static_cast<int16_t>(joy.accum_x + dx);
  joy.accum_y = static_cast<int16_t>(joy.accum_y + dy);
  joy.raw_x = x;
  joy.raw_y = y;
}


// $c6 motion step: proximity cull + optional ×15/16 damp + position integrate.
// Returns true (d0=1) on success / object processed.
// See docs/func_c6.md for the full path-B / accept-path analysis.
inline bool object_motion_step(Object& obj, const ViewDesc& view,
                               bool gate_a = false, bool gate_b = false) {
  if (gate_a || gate_b)
    return false;  // early exit when $18d | $17e nonzero

  // Coarse proximity: distance vs view descriptor (original uses asr#8 high-byte)
  auto dist = [](float v) { return std::fabs(v); };
  bool near = dist(obj.pos.x - view.axis0) < float(view.threshold_coarse)
           && dist(obj.pos.y - view.axis1) < float(view.threshold_coarse)
           && dist(obj.pos.z - view.axis2) < float(view.threshold_coarse);

  Vec3& vel = near ? obj.vel_near : obj.vel_far;

  // Damping ×15/16 when type_flags bits 4 or 5 set (object+$26)
  const bool damp = (obj.type_flags & 0x30) != 0;
  if (damp) {
    vel.x -= vel.x / 16.f;
    vel.y -= vel.y / 16.f;
    vel.z -= vel.z / 16.f;
  }

  // Integrate into position (+0/+4/+8)
  obj.pos.x += vel.x;
  obj.pos.y += vel.y;
  obj.pos.z += vel.z;

  // Mark active (accept path sets object+$24 = 1 on first touch)
  if (obj.cos_q14 == 0)
    obj.cos_q14 = 1;

  obj.state = 1;  // processed this frame
  return true;
}

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
  ViewDesc view{};

  void clear() { count = 0; }
  Object* add() {
    if (count >= kMaxObjects) return nullptr;
    return &objects[count++];
  }
  // $c6 motion step for every active object (before transform/draw)
  void motion_step_all() {
    for (int i = 0; i < count; ++i)
      object_motion_step(objects[i], view);
  }
  // One frame of object_loop $72dc: motion → transform → draw
  void draw_all_fp(SDL_Renderer* ren, GfxState& gfx) {
    motion_step_all();
    for (int i = 0; i < count; ++i)
      draw_object_fp(ren, gfx, objects[i]);
  }
};

} // namespace fa18
