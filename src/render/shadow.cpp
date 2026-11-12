#include "render/shadow.hpp"

#include "native/gl_renderer.hpp"
#include "native/globals.hpp"
#include "native/orig.hpp"
#include "native/terrain.hpp"
#include "native/vars.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace render {
namespace {

using amiga::Machine;

// ---- small matrix / quaternion helpers (double, row-major 3x3) ----------------

struct M3 {
  double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

M3 mul(const M3& a, const M3& b) {
  M3 r;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      r.m[i * 3 + j] = a.m[i * 3] * b.m[j] + a.m[i * 3 + 1] * b.m[3 + j] + a.m[i * 3 + 2] * b.m[6 + j];
  return r;
}

bool inverse(const M3& a, M3& out) {
  const double* m = a.m;
  const double c0 = m[4] * m[8] - m[5] * m[7], c1 = m[5] * m[6] - m[3] * m[8],
               c2 = m[3] * m[7] - m[4] * m[6];
  const double det = m[0] * c0 + m[1] * c1 + m[2] * c2;
  if (std::fabs(det) < 1e-12) return false;
  const double k = 1.0 / det;
  out.m[0] = c0 * k;
  out.m[1] = (m[2] * m[7] - m[1] * m[8]) * k;
  out.m[2] = (m[1] * m[5] - m[2] * m[4]) * k;
  out.m[3] = c1 * k;
  out.m[4] = (m[0] * m[8] - m[2] * m[6]) * k;
  out.m[5] = (m[2] * m[3] - m[0] * m[5]) * k;
  out.m[6] = c2 * k;
  out.m[7] = (m[1] * m[6] - m[0] * m[7]) * k;
  out.m[8] = (m[0] * m[4] - m[1] * m[3]) * k;
  return true;
}

struct Quat {
  double w = 1, x = 0, y = 0, z = 0;
};

Quat to_quat(const M3& r) {
  const double* m = r.m;
  Quat q;
  const double tr = m[0] + m[4] + m[8];
  if (tr > 0) {
    const double s = std::sqrt(tr + 1.0) * 2;
    q = {0.25 * s, (m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s};
  } else if (m[0] > m[4] && m[0] > m[8]) {
    const double s = std::sqrt(1.0 + m[0] - m[4] - m[8]) * 2;
    q = {(m[7] - m[5]) / s, 0.25 * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s};
  } else if (m[4] > m[8]) {
    const double s = std::sqrt(1.0 + m[4] - m[0] - m[8]) * 2;
    q = {(m[2] - m[6]) / s, (m[1] + m[3]) / s, 0.25 * s, (m[5] + m[7]) / s};
  } else {
    const double s = std::sqrt(1.0 + m[8] - m[0] - m[4]) * 2;
    q = {(m[3] - m[1]) / s, (m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25 * s};
  }
  return q;
}

M3 to_matrix(const Quat& q) {
  M3 r;
  const double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
  const double xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
  const double wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
  r.m[0] = 1 - 2 * (yy + zz);
  r.m[1] = 2 * (xy - wz);
  r.m[2] = 2 * (xz + wy);
  r.m[3] = 2 * (xy + wz);
  r.m[4] = 1 - 2 * (xx + zz);
  r.m[5] = 2 * (yz - wx);
  r.m[6] = 2 * (xz - wy);
  r.m[7] = 2 * (yz + wx);
  r.m[8] = 1 - 2 * (xx + yy);
  return r;
}

// Rotation between two orientations along the shorter arc.
M3 slerp(const M3& a, const M3& b, double t) {
  Quat p = to_quat(a), q = to_quat(b);
  double d = p.w * q.w + p.x * q.x + p.y * q.y + p.z * q.z;
  if (d < 0) {
    q = {-q.w, -q.x, -q.y, -q.z};
    d = -d;
  }
  double ka = 1 - t, kb = t;
  if (d < 0.9999995) {
    const double th = std::acos(std::min(1.0, d)), s = std::sin(th);
    ka = std::sin((1 - t) * th) / s;
    kb = std::sin(t * th) / s;
  }
  Quat r{ka * p.w + kb * q.w, ka * p.x + kb * q.x, ka * p.y + kb * q.y, ka * p.z + kb * q.z};
  const double n = std::sqrt(r.w * r.w + r.x * r.x + r.y * r.y + r.z * r.z);
  r = {r.w / n, r.x / n, r.y / n, r.z / n};
  return to_matrix(r);
}

// ---- the original's view rotations, in floating point --------------------------
// Angles are words of 1/80 degree (28800 = 360 degrees).  The original
// drops the low three bits and reads a Q14 sine table; these are the same
// products without the table and the rounding (view.cpp, $42bbe / $42b6e).

constexpr double kTurn = 28800.0;

void sincos(double units, double& s, double& c) {
  const double a = units * (2.0 * M_PI / kTurn);
  s = std::sin(a);
  c = std::cos(a);
}

M3 three_angles(double a0, double a2, double a4) {  // $42bbe
  double s0, c0, s2, c2, s4, c4;
  sincos(a0, s0, c0);
  sincos(a2, s2, c2);
  sincos(a4, s4, c4);
  M3 r;
  r.m[0] = s2 * s0 * s4 + c2 * c4;
  r.m[1] = -(c0 * s4);
  r.m[2] = s2 * c4 - c2 * s0 * s4;
  r.m[3] = c2 * s4 - s2 * s0 * c4;
  r.m[4] = c0 * c4;
  r.m[5] = c2 * s0 * c4 + s2 * s4;
  r.m[6] = -(s2 * c0);
  r.m[7] = -s0;
  r.m[8] = c2 * c0;
  return r;
}

M3 two_angles(double a0, double a2) {  // $42b6e
  double s0, c0, s2, c2;
  sincos(a0, s0, c0);
  sincos(a2, s2, c2);
  M3 r;
  r.m[0] = c2;
  r.m[1] = 0;
  r.m[2] = s2;
  r.m[3] = -(s2 * s0);
  r.m[4] = c0;
  r.m[5] = c2 * s0;
  r.m[6] = -(s2 * c0);
  r.m[7] = -s0;
  r.m[8] = c2 * c0;
  return r;
}

// ---- camera of one game frame --------------------------------------------------

struct CamState {
  bool external = false;
  int16_t m[9] = {};       // $5aed0: quantised, row-scaled view matrix
  int16_t scale[3] = {};   // $5ad36/$5ad38/$5ad3a
  int32_t neg[3] = {};     // $5ad5a/$5ad5e/$5ad62: -(eye), inside the sector
  int32_t eye[3] = {};     // $5ad74/$5ad78/$5ad7c
  int16_t sector[2] = {};  // $5ac44/$5ac46
  M3 rot;                  // exact rotation from the view angles
};

int16_t s16(uint32_t v) { return int16_t(uint16_t(v)); }

CamState read_camera(Machine& m) {
  CamState c;
  c.external = m.r8(0x5aa7d) != 0;
  for (int i = 0; i < 9; ++i) c.m[i] = s16(m.r16(0x5aed0 + 2u * uint32_t(i)));
  for (int i = 0; i < 3; ++i) {
    c.scale[i] = s16(m.r16(0x5ad36 + 2u * uint32_t(i)));
    c.neg[i] = int32_t(m.r32(0x5ad5a + 4u * uint32_t(i)));
    c.eye[i] = int32_t(m.r32(0x5ad74 + 4u * uint32_t(i)));
  }
  c.sector[0] = s16(m.r16(0x5ac44));
  c.sector[1] = s16(m.r16(0x5ac46));
  if (c.external)
    c.rot = two_angles(m.r16(0x5ad8c), m.r16(0x5ad8e));
  else
    c.rot = three_angles(m.r16(0x5ad82), m.r16(0x5ad86), m.r16(0x5ad8a));
  return c;
}

// ---- primitive capture ---------------------------------------------------------
// Hooks on the entry points of the original's rasterisers.  On the shadow
// the leaves are stubs (nothing is drawn); on the game (verification only)
// the same hooks observe the real drawing.

struct Capture {
  Machine* m = nullptr;
  bool on = false;
  std::vector<Vec3> verts;
  std::vector<Prim> prims;
  // A point is projected by $4389c.. and plotted by one of four leaves; the
  // leaf decides its size.
  bool point_pending = false;
  Vec3 point{};
  uint64_t fills_2d = 0;
  // The model being drawn ($31e4a: mesh header chosen), for snap().
  bool in_object = false;
  uint32_t mesh = 0;
  uint8_t mesh_flags = 0;
  bool snap_on = false;
  uint64_t snapped = 0, unsnapped = 0;
  bool stubbed = false;
  bool stubs_on = true;  // off while the cockpit is drawn for real

  void clear() {
    verts.clear();
    prims.clear();
    point_pending = false;
    in_object = false;
  }

  uint16_t w(uint32_t a) const { return m->r16(a); }

  PlaneOp poly_op() const {
    PlaneOp o;
    const uint16_t mask = w(0x5a9de) & 15;
    const int16_t w1 = s16(w(0x5a9e0));
    if (w1 >= 0 && w(0x5a9e4) != 0) {
      o.copy3to2 = true;
      return o;
    }
    const uint16_t w2 = w(0x5a9e2), col = w(0x5ac4c);
    for (int b = 0; b < 4; ++b) {
      if (!(mask & (1 << b))) continue;
      bool xr = (w2 >> b) & 1;
      if (w1 < 0) xr = w2 & 1;
      const bool set = w1 < 0 ? ((col >> b) & 1) : ((uint16_t(w1) >> b) & 1);
      if (xr) o.xr |= uint8_t(1 << b);
      else if (set) o.set |= uint8_t(1 << b);
      else o.clr |= uint8_t(1 << b);
    }
    return o;
  }
  PlaneOp line_op() const {
    PlaneOp o;
    const uint16_t mask = w(0x5a9de) & 15;
    const int16_t w1 = s16(w(0x5a9e0));
    const uint16_t col = w(0x5ac4c);  // $44686 copies it to $5ac4e first
    for (int b = 0; b < 4; ++b) {
      if (!(mask & (1 << b))) continue;
      const bool set = w1 < 0 ? ((col >> b) & 1) : ((uint16_t(w1) >> b) & 1);
      if (set) o.set |= uint8_t(1 << b);
      else o.clr |= uint8_t(1 << b);
    }
    return o;
  }
  PlaneOp point_op() const {  // $44290: colour bits into the masked planes
    PlaneOp o;
    const uint8_t mask = uint8_t(w(0x5a9de) & 15), col = uint8_t(w(0x5ac4c) & 15);
    o.set = uint8_t(col & mask);
    o.clr = uint8_t(~col & mask);
    return o;
  }

  Prim& add(Prim::Kind kind, const PlaneOp& op, int count) {
    Prim p;
    p.kind = kind;
    p.op = op;
    p.first = uint32_t(verts.size());
    p.count = uint16_t(count);
    prims.push_back(p);
    return prims.back();
  }

  void on_object(uint32_t mesh_header, bool distant) {
    in_object = on && !distant;  // distant ground objects: other frame layout, not snapped
    mesh = mesh_header;
    mesh_flags = m->r8(mesh_header + 7);
  }

  // The original transforms model vertices to 16-bit view-space words, so a
  // vertex is up to a unit or two off and moves in unit steps while the
  // camera moves.  If the captured words (x, y, z) are the transform of a
  // vertex of the current model, return that vertex transformed without the
  // rounding, in the same view space.  Checked against the words: anything
  // that is not a plain model vertex (derived points, shadows, effects)
  // stays as it is.
  //
  // $328ea: pool = M ((B (v >> s8)) + off) >> 8, with off = (fp-32 + $5ae28,
  // fp-28 [+ $5ae2c], fp-24 + $5ae30) >> (8 - fp-6), M = $5aed0 and B the
  // object's own rotation $5aebe (1/256) for articulated models.
  bool snap(int16_t x, int16_t y, int16_t z, Vec3& out) {
    if (!snap_on || !in_object) return false;
    const uint32_t pool = 0x5d684;
    const uint16_t wx = uint16_t(x), wy = uint16_t(y), wz = uint16_t(z);
    const uint32_t fp = m->cpu.a[6];
    const int count = std::max<int>(m->r8(mesh + 8), 1);
    const int limit = std::min(count + 64, 250);
    const bool articulated = mesh_flags & 1, flat = !articulated && (mesh_flags & 2);
    for (int j = 0; j < limit; ++j) {
      const uint32_t slot = pool + 6u * uint32_t(j);
      if (w(slot) != wx || w(slot + 2) != wy || w(slot + 4) != wz) continue;
      const int s8 = s16(w(fp - 8)) & 15, sh = 8 - int(s16(w(fp - 6)));
      if (sh < 0 || sh > 24) return false;
      const uint32_t src = mesh + 10 + (flat ? 4u : 6u) * uint32_t(j);
      double q[3];
      if (flat) {
        q[0] = double(s16(w(src)) >> s8);
        q[1] = 0;
        q[2] = double(s16(w(src + 2)) >> s8);
      } else {
        for (int i = 0; i < 3; ++i) q[i] = double(s16(w(src + 2u * uint32_t(i))) >> s8);
      }
      if (articulated) {
        double r[3];
        for (int i = 0; i < 3; ++i)
          r[i] = (q[0] * s16(w(0x5aebe + 6u * uint32_t(i))) + q[1] * s16(w(0x5aebe + 6u * uint32_t(i) + 2)) +
                  q[2] * s16(w(0x5aebe + 6u * uint32_t(i) + 4))) / 256.0;
        for (int i = 0; i < 3; ++i) q[i] = r[i];
      }
      const double k = 1.0 / double(1u << sh);
      const double ox = double(int32_t(m->r32(fp - 32)) + int32_t(m->r32(0x5ae28))) * k;
      const double oz = double(int32_t(m->r32(fp - 24)) + int32_t(m->r32(0x5ae30))) * k;
      const double oy0 = double(int32_t(m->r32(fp - 28))) * k;
      const double oy1 = double(int32_t(m->r32(fp - 28)) + int32_t(m->r32(0x5ae2c))) * k;
      for (double oy : {articulated ? oy1 : oy0, articulated ? oy0 : oy1}) {
        const double p[3] = {q[0] + ox, q[1] + oy, q[2] + oz};
        double v[3];
        for (int i = 0; i < 3; ++i)
          v[i] = (p[0] * s16(w(0x5aed0 + 6u * uint32_t(i))) + p[1] * s16(w(0x5aed0 + 6u * uint32_t(i) + 2)) +
                  p[2] * s16(w(0x5aed0 + 6u * uint32_t(i) + 4))) / 256.0;
        if (std::fabs(v[0] - x) < 3.5 && std::fabs(v[1] - y) < 3.5 && std::fabs(v[2] - z) < 3.5) {
          out = {float(v[0]), float(v[1]), float(v[2])};
          ++snapped;
          return true;
        }
      }
    }
    ++unsnapped;
    return false;
  }
  Vec3 vertex(uint32_t at, int shift) {
    const int16_t x = s16(w(at)), y = s16(w(at + 2)), z = s16(w(at + 4));
    Vec3 v;
    if (snap(x, y, z, v)) {
      const float k = float(1u << shift);
      return {v.x * k, v.y * k, v.z * k};
    }
    auto sh = [&](int16_t c) { return float(int16_t(uint32_t(int32_t(c)) << shift)); };
    return {sh(x), sh(y), sh(z)};
  }

  void on_polygon() {  // $38c0c: $61284 shift, $61286 count, $61288 vertices
    if (!on) return;
    const int shift = s16(w(0x61284)) & 31;
    const int n = s16(w(0x61286));
    if (n < 3 || n > 40) return;
    add(Prim::Poly, poly_op(), n);
    for (int i = 0; i < n; ++i) verts.push_back(vertex(0x61288 + 6u * uint32_t(i), shift));
  }
  void on_line(bool clipped) {  // $43a56 (clips) / $4397c (rejects)
    if (!on) return;
    Vec3 p[2];
    for (int i = 0; i < 2; ++i) {
      const uint32_t a = 0x61886 + 6u * uint32_t(i);
      p[i] = {float(s16(w(a))), float(s16(w(a + 2))), float(s16(w(a + 4)))};
      if (!clipped && (p[i].z <= 0 || std::fabs(p[i].x) > p[i].z || std::fabs(p[i].y) > p[i].z))
        return;
    }
    if (clipped && p[0].z < 1 && p[1].z < 1) return;
    for (int i = 0; i < 2; ++i) p[i] = vertex(0x61886 + 6u * uint32_t(i), 0);
    add(Prim::Line, line_op(), 2);
    verts.push_back(p[0]);
    verts.push_back(p[1]);
  }
  void on_project() {  // $4389c..$438b4: d0-d2 = view-space point
    const m68k::Cpu& c = m->cpu;
    const int16_t x = s16(c.d[0]), y = s16(c.d[1]), z = s16(c.d[2]);
    point = {float(x), float(y), float(z)};
    // the projector's own tests: outside the view nothing is plotted
    point_pending = on && z > 0 && std::abs(int(x)) < z && std::abs(int(y)) < z;
    if (point_pending) snap(x, y, z, point);
  }
  void on_plot(int leaf) {  // $441fc 1x1, $44212 2x1, $44276 2x2, $43dcc disc
    if (!on) return;
    const m68k::Cpu& c = m->cpu;
    Vec3 at = point;
    if (!point_pending) {
      // Not a primitive of its own: the fill and line routines plot
      // polygons and lines of one or two pixels as points ($44dfe, $44686),
      // and $44212 plots twice.  The
      // renderer applies the same minimum size to small polygons.
      const uint32_t ret = m->r32(c.a[7]);
      if ((ret >= 0x44600 && ret < 0x44f00) || (ret >= 0x44212 && ret < 0x4422c) ||
          ret == 0x43dca)
        return;
      // Plotted directly at screen column d0, row d1 (ground dots): the
      // view direction that projects there (points land one row lower).
      if (!stubbed && std::getenv("FA18_SHADOW_PLOTS"))
        std::fprintf(stderr, "plot leaf %d ret %06x ret2 %06x\n", leaf, ret, m->r32(c.a[7] + 4));
      at = {(159.f - float(s16(c.d[0]))) / 160.f, (90.f - float(s16(c.d[1]))) / 90.f, 1.f};
    }
    point_pending = false;
    if (leaf == 3) {
      const int r = s16(c.d[6]);
      if (r >= 1) {
        Prim& p = add(Prim::Disc, poly_op(), 1);
        p.radius = int16_t(std::min(r, 127));
        verts.push_back(at);
        return;
      }
      leaf = 1;  // $43dc4: radius 0 plots 2x1
    }
    if (leaf == 2 && s16(c.d[1]) >= s16(w(0x5ac7c))) leaf = 1;  // last row
    Prim& p = add(Prim::Point, point_op(), 1);
    p.w = leaf == 0 ? 1 : 2;
    p.h = leaf == 2 ? 2 : 1;
    verts.push_back(at);
  }
  void on_fill() {  // $44b50: screen polygon at $60684; 3D polygons come from $38c0c
    if (!on) return;
    if (m->r32(m->cpu.a[7]) == 0x392d2) return;
    const int n = s16(w(0x60684));
    if (n < 3 || n > 60) return;
    ++fills_2d;
    add(Prim::Poly, poly_op(), n);
    for (int i = 0; i < n; ++i) {
      const float x = float(s16(w(0x60686 + 4u * uint32_t(i))));
      const float y = float(s16(w(0x60688 + 4u * uint32_t(i))));
      // the view direction that projects to this screen position
      verts.push_back({(159.f - x) / 160.f, (89.f - y) / 90.f, 1.f});
    }
  }

  void install(Machine& machine, bool stub) {
    m = &machine;
    machine.add_pc_hook(0x38c0c, [this] { on_polygon(); });
    machine.add_pc_hook(0x43a56, [this] { on_line(true); });
    machine.add_pc_hook(0x4397c, [this] { on_line(false); });
    for (uint32_t a : {0x4389cu, 0x438a0u, 0x438a8u, 0x438acu, 0x438b0u, 0x438b4u})
      machine.add_pc_hook(a, [this] { on_project(); });
    machine.add_pc_hook(0x441fc, [this] { on_plot(0); });
    machine.add_pc_hook(0x44212, [this] { on_plot(1); });
    machine.add_pc_hook(0x44276, [this] { on_plot(2); });
    machine.add_pc_hook(0x43dcc, [this] { on_plot(3); });
    machine.add_pc_hook(0x44b50, [this] { on_fill(); });
    stubbed = stub;
    if (!stub) return;
    // Nothing is drawn on the shadow: fills, lines and point plots return
    // at once.  $38c0c itself runs (it clips and tells its caller whether
    // anything was visible; the model code depends on that).
    for (uint32_t a : {0x44b50u, 0x44686u, 0x441fcu, 0x44212u, 0x44276u, 0x43dccu})
      machine.add_stub(a, [this] { return stubs_on; });
  }
};

bool same_prims(const Capture& a, const Capture& b, std::string& why) {
  char buf[160];
  if (a.prims.size() != b.prims.size()) {
    std::snprintf(buf, sizeof buf, "primitive count %zu vs %zu", a.prims.size(), b.prims.size());
    why = buf;
    return false;
  }
  for (size_t i = 0; i < a.prims.size(); ++i) {
    const Prim &p = a.prims[i], &q = b.prims[i];
    if (p.kind != q.kind || p.count != q.count || !(p.op == q.op) || p.w != q.w || p.h != q.h ||
        p.radius != q.radius) {
      std::snprintf(buf, sizeof buf, "primitive %zu differs (kind %d/%d count %d/%d)", i,
                    int(p.kind), int(q.kind), int(p.count), int(q.count));
      why = buf;
      return false;
    }
    for (int k = 0; k < p.count; ++k) {
      const Vec3 &u = a.verts[p.first + size_t(k)], &v = b.verts[q.first + size_t(k)];
      if (u.x != v.x || u.y != v.y || u.z != v.z) {
        std::snprintf(buf, sizeof buf, "primitive %zu vertex %d: %g,%g,%g vs %g,%g,%g", i, k,
                      double(u.x), double(u.y), double(u.z), double(v.x), double(v.y), double(v.z));
        why = buf;
        return false;
      }
    }
  }
  return true;
}

// ---- moving objects between two game frames ------------------------------------

struct Ram {
  const std::vector<uint8_t>& r;
  bool ok(uint32_t a, uint32_t n) const { return size_t(a) + n <= r.size(); }
  uint16_t w(uint32_t a) const { return uint16_t(r[a] << 8 | r[a + 1]); }
  uint32_t l(uint32_t a) const { return uint32_t(w(a)) << 16 | w(a + 2); }
};

// A 3x3 word matrix as a rotation, if it is one (Q14).
bool rotation_q14(const Ram& m, uint32_t a, M3& out) {
  for (int i = 0; i < 9; ++i) out.m[i] = double(s16(m.w(a + 2u * uint32_t(i)))) / 16384.0;
  const double* r = out.m;
  for (int i = 0; i < 3; ++i) {
    const double n = r[i * 3] * r[i * 3] + r[i * 3 + 1] * r[i * 3 + 1] + r[i * 3 + 2] * r[i * 3 + 2];
    if (std::fabs(n - 1.0) > 0.03) return false;
  }
  const double det = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) +
                     r[2] * (r[3] * r[7] - r[4] * r[6]);
  return det > 0.9;
}

// Positions and attitudes of the scenario objects (aircraft, ships,
// missiles: 16 records of 512 bytes at $5b47c), the small records ($5d47c)
// and the effect slots ($5af6a) at `t` between two images, written into the
// machine that holds the newer one.  Anything that does not look like the
// same object moving a plausible distance keeps its newer state.
void interpolate_objects(Machine& sm, const std::vector<uint8_t>& older,
                         const std::vector<uint8_t>& newer, double t) {
  const Ram a{older}, b{newer};
  if (!a.ok(0x5d47c + 16 * 32, 0) || !b.ok(0x5d47c + 16 * 32, 0)) return;
  auto lerp_long = [&](uint32_t at, double shift_a) {
    const double x0 = double(int32_t(a.l(at))) + shift_a, x1 = double(int32_t(b.l(at)));
    if (std::fabs(x1 - x0) > 1048576.0) return;  // teleport / new object
    sm.w32(at, uint32_t(int32_t(std::lround(x0 + (x1 - x0) * t))));
  };
  for (int i = 0; i < 16; ++i) {
    const uint32_t rec = 0x5b47c + 512u * uint32_t(i), type = 0x36094 + 20u * uint32_t(i);
    // in use in both frames, with the same model
    if (!(a.r[rec + 1] & 0x40) || !(b.r[rec + 1] & 0x40)) continue;
    if (std::memcmp(&older[type], &newer[type], 12) != 0) continue;
    const double sx = double(int32_t(s16(a.w(rec + 6)) - s16(b.w(rec + 6)))) * 4194304.0;
    const double sz = double(int32_t(s16(a.w(rec + 8)) - s16(b.w(rec + 8)))) * 4194304.0;
    lerp_long(rec + 20, sx);
    lerp_long(rec + 24, 0);
    lerp_long(rec + 28, sz);
    for (uint32_t off : {102u, 104u, 106u}) {  // attitude angles, 28800 per turn
      double d = double(s16(b.w(rec + off))) - double(s16(a.w(rec + off)));
      d -= kTurn * std::floor(d / kTurn + 0.5);
      if (std::fabs(d) > 3600) continue;
      double v = double(s16(a.w(rec + off))) + d * t;
      v -= kTurn * std::floor(v / kTurn);
      sm.w16(rec + off, uint16_t(int(std::lround(v)) % 28800));
    }
    for (uint32_t off : {128u, 146u}) {  // attitude matrices
      M3 r0, r1;
      if (!rotation_q14(a, rec + off, r0) || !rotation_q14(b, rec + off, r1)) continue;
      const M3 r = slerp(r0, r1, t);
      for (int k = 0; k < 9; ++k)
        sm.w16(rec + off + 2u * uint32_t(k), uint16_t(int16_t(std::lround(r.m[k] * 16384.0))));
    }
  }
  for (int i = 0; i < 16; ++i) {  // small records: x, z words and a y long
    const uint32_t rec = 0x5d47c + 32u * uint32_t(i);
    if (a.l(rec) != b.l(rec)) continue;
    for (uint32_t off : {12u, 14u}) {
      const double x0 = double(s16(a.w(rec + off))), x1 = double(s16(b.w(rec + off)));
      if (std::fabs(x1 - x0) > 4096) continue;
      sm.w16(rec + off, uint16_t(int16_t(std::lround(x0 + (x1 - x0) * t))));
    }
    lerp_long(rec + 16, 0);
  }
  for (int i = 0; i < 20; ++i) {  // effect slots: position longs, active bit at +39
    const uint32_t rec = 0x5af6a + 64u * uint32_t(i);
    if (!(a.r[rec + 39] & 1) || !(b.r[rec + 39] & 1)) continue;
    if (a.w(rec + 46) != b.w(rec + 46)) continue;
    // the same effect is one step older in the newer frame
    const int age = int(s16(a.w(rec + 40))) - int(s16(b.w(rec + 40)));
    if (age < 0 || age > 1) continue;
    for (uint32_t off : {0u, 4u, 8u}) lerp_long(rec + off, 0);
  }
}

// The shadow is the "current machine" of the translated routines while it runs.
struct MachineScope {
  Machine* saved;
  explicit MachineScope(Machine* m) : saved(native::g_machine) { native::g_machine = m; }
  ~MachineScope() { native::g_machine = saved; }
};

}  // namespace

struct Shadow::Impl {
  Machine* game = nullptr;
  Machine sm;
  Capture cap, game_cap;
  struct Snap {
    Machine::Image image;
    CamState cam;
    bool valid = false;
  };
  Snap cur, prev;
  Machine::Image pending;
  bool pending_valid = false;
  uint64_t serial = 0;
  bool failed = false;
  bool verify = false;
  uint64_t verified = 0, mismatched = 0;

  void on_view_setup() {  // game: entry of $4217c, before this frame's view
    game->capture_image(pending);
    pending_valid = true;
    if (verify) {
      game_cap.clear();
      game_cap.on = false;
    }
  }
  void on_scene_start() {  // game: entry of $43306, view and camera are final
    if (!pending_valid) return;
    pending_valid = false;
    std::swap(prev, cur);
    std::swap(cur.image, pending);  // keeps the buffers' capacity
    cur.cam = read_camera(*game);
    cur.valid = true;
    ++serial;
    if (verify) game_cap.on = true;
  }
  void on_scene_done();  // game: $23d54, after the 3D scene

  Machine::Image before_2d;
  std::vector<uint8_t> planes_a;
  bool overlay_enabled = true;
  bool snap_enabled = std::getenv("FA18_NO_SNAP") == nullptr;
  uint64_t partial_pixels = 0;
  bool profile = std::getenv("FA18_SHADOW_PROFILE") != nullptr;
  // FA18_SHADOW_PROFILE=N: report every N pictures (default 300)
  uint64_t prof_every = std::max(1, std::atoi(std::getenv("FA18_SHADOW_PROFILE") ? std::getenv("FA18_SHADOW_PROFILE") : "300"));
  double prof[3] = {};
  uint64_t prof_n = 0;

  bool run(float alpha, float widen, Frame& out);
  void draw_2d();
  void fill_view(uint8_t value);
  void read_planes(std::vector<uint8_t>& out);
};

// The rest of the game's frame after the 3D scene: cockpit instruments, HUD
// and message line (flow.cpp, game_frame), without sound, timing and the
// frame limiter.  Runs on the shadow with the real drawing routines.
void Shadow::Impl::draw_2d() {
  using namespace native;
  using namespace native::g;
  using namespace native::v;
  const int16_t frame = int16_t(rd16(0x5abd2));  // frame counter
  sub_23d54();
  if (b_5aa7d == 0 || b_5ab2e != 0) {
    const uint32_t plane = uint32_t(int32_t(int16_t(w_5abd4))) << 9;
    const uint8_t c = rd8(0x5b47c + plane + 98);
    if (c != '0' || b_5ab2e != 0) {
      sub_45368();
      sub_455ba();
      sub_45e2a();
      sub_47ec0();
      sub_45d2e();
      sub_45b7c();
      const int8_t boost = b_5ab2f;
      if (boost > 0 || (frame & 3) > 1) {
        sub_46aba();
        sub_46b50();
      }
      if (b_5ab2f > 0 || (frame & 3) < 2) sub_46d7c();
      if (b_5ab2f > 0 || (frame & 3) < 2 || l_5ad5e > -32768) sub_46c1e();
      if (b_5ab2f > 0 || (frame & 7) < 2) sub_46d2e();
      if (b_5ab2f > 0 || (frame & 15) < 2) sub_4551c();
      sub_44c42();
      sub_474ac();
      sub_45604();
      sub_46dd6();
      sub_46e64();
      sub_466d0();
      sub_45938();
      sub_46668();
      sub_46ef2();
      sub_45760();
    }
  } else if (b_5aa7d != 0) {
    b_5aad1 = 1;
    sub_46b50();
    sub_46c1e();
    sub_46aba();
    if (rd8(0x5ab9e) == 2) sub_46ef2();  // selected mission
  }
  sub_2439a();
  sub_46b4e();
  if (b_5aaaa != 0) {
    sub_440a4();
    sub_4677a();
  }
  sub_478f2();
}

// The four bitplanes of the buffer the game draws into ($5a9ae).
void Shadow::Impl::fill_view(uint8_t value) {
  const uint32_t tbl = sm.r32(0x5a9ae);
  const int rows = std::clamp(int(s16(sm.r16(0x5ac7c))) + 1, 0, 200);
  for (int pl = 0; pl < 4; ++pl) {
    const uint32_t base = sm.r32(tbl + 4u * uint32_t(pl));
    if (base < 0x400 || base + 8000 > sm.ram_used()) return;
    std::memset(sm.ram_data() + base, value, size_t(rows) * 40);
  }
}
void Shadow::Impl::read_planes(std::vector<uint8_t>& out) {
  out.assign(320 * 200, 0);
  const uint32_t tbl = sm.r32(0x5a9ae);
  for (int pl = 0; pl < 4; ++pl) {
    const uint32_t base = sm.r32(tbl + 4u * uint32_t(3 - pl));  // table: plane 3 first
    if (base < 0x400 || base + 8000 > sm.ram_used()) continue;
    const uint8_t* src = sm.ram_data() + base;
    for (int i = 0; i < 320 * 200; ++i)
      if (src[i >> 3] & (0x80 >> (i & 7))) out[size_t(i)] |= uint8_t(1 << pl);
  }
}

void Shadow::Impl::on_scene_done() {
  if (!verify || !game_cap.on) return;
  game_cap.on = false;
  Frame f;
  if (!run(1.f, 1.f, f)) return;
  std::string why;
  ++verified;
  if (!same_prims(game_cap, cap, why)) {
    ++mismatched;
    if (mismatched <= 3 && std::getenv("FA18_SHADOW_DUMP")) {
      for (const Capture* c : {&game_cap, &cap}) {
        std::fprintf(stderr, "  %s:\n", c == &game_cap ? "game" : "shadow");
        for (const Prim& p : c->prims) {
          std::fprintf(stderr, "   k%d n%d set%x clr%x xr%x cp%d %dx%d r%d:", int(p.kind), int(p.count),
                       p.op.set, p.op.clr, p.op.xr, int(p.op.copy3to2), p.w, p.h, p.radius);
          for (int k = 0; k < p.count && k < 4; ++k)
            std::fprintf(stderr, " (%g,%g,%g)", double(c->verts[p.first + size_t(k)].x),
                         double(c->verts[p.first + size_t(k)].y), double(c->verts[p.first + size_t(k)].z));
          std::fprintf(stderr, "\n");
        }
      }
    }
    if (mismatched <= 20)
      std::fprintf(stderr, "shadow: frame %llu differs from the game: %s\n",
                   (unsigned long long)game->frame_count(), why.c_str());
  }
  if (verified % 200 == 0 || (mismatched && mismatched <= 20))
    std::fprintf(stderr, "shadow: verified %llu frames, %llu differ (%zu primitives)\n",
                 (unsigned long long)verified, (unsigned long long)mismatched, cap.prims.size());
}

bool Shadow::Impl::run(float alpha, float widen, Frame& out) {
  if (!cur.valid || failed) return false;
  alpha = std::clamp(alpha, 0.f, 1.f);
  if (widen < 1.f) widen = 1.f;
  bool interpolate = alpha < 1.f && prev.valid && prev.cam.external == cur.cam.external &&
                     cur.image.frame - prev.image.frame <= 16 && prev.image.ram.size() == cur.image.ram.size();
  if (interpolate) {
    // a jump of the camera (view change, restart) is not a movement
    for (int i = 0; i < 3; ++i) {
      double d = double(cur.cam.neg[i]) - double(prev.cam.neg[i]);
      if (i == 0) d -= double(int32_t(cur.cam.sector[0] - prev.cam.sector[0])) * 4194304.0;
      if (i == 2) d -= double(int32_t(cur.cam.sector[1] - prev.cam.sector[1])) * 4194304.0;
      if (std::fabs(d) > 1048576.0) interpolate = false;
    }
  }
  MachineScope scope(&sm);
  using Clock = std::chrono::steady_clock;
  const auto t_start = Clock::now();
  auto t_scene = t_start, t_2d = t_start;
  sm.restore_image(cur.image);
  const auto t_restore = Clock::now();
  cap.clear();
  CamState cam;
  M3 rot = cur.cam.rot;
  try {
    using namespace native;
    if (interpolate) interpolate_objects(sm, prev.image.ram, cur.image.ram, double(alpha));
    sub_4217c();
    cam = read_camera(sm);
    if (interpolate) rot = slerp(prev.cam.rot, cur.cam.rot, alpha);
    if (interpolate || widen != 1.f) {
      // The matrix the original code culls and transforms with: the exact
      // rotation, row-scaled and rounded like its own.
      for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
          double v = rot.m[r * 3 + c] * double(cam.scale[r]);
          if (r == 0) v /= double(widen);
          sm.w16(0x5aed0 + 2u * uint32_t(r * 3 + c), uint16_t(int16_t(std::lround(v))));
        }
    }
    sub_2f4a6();
    if (interpolate) {
      // Camera position between the two frames, in the newest frame's sector.
      int32_t neg[3];
      for (int i = 0; i < 3; ++i) {
        double a = double(prev.cam.neg[i]);
        if (i == 0) a += double(int32_t(cur.cam.sector[0] - prev.cam.sector[0])) * 4194304.0;
        if (i == 2) a += double(int32_t(cur.cam.sector[1] - prev.cam.sector[1])) * 4194304.0;
        neg[i] = int32_t(std::lround(a + (double(cur.cam.neg[i]) - a) * double(alpha)));
        sm.w32(0x5ad5a + 4u * uint32_t(i), uint32_t(neg[i]));
        sm.w16(0x5ad6a + 2u * uint32_t(i), uint16_t(neg[i] >> 8));
        const double e0 = double(prev.cam.eye[i]), e1 = double(cur.cam.eye[i]);
        if (std::fabs(e1 - e0) < 2097152.0)
          sm.w32(0x5ad74 + 4u * uint32_t(i), uint32_t(int32_t(std::lround(e0 + (e1 - e0) * double(alpha)))));
      }
      sm.w32(0x5ad70, uint32_t(neg[1] >> 8));
    }
    sub_39a54();
    sub_2f7b8();
    sub_39b06();
    sub_39dd0();
    // Horizon and terrain are drawn by the renderer itself, but they leave
    // the plane masks the sun is drawn with.
    sub_42f48();
    if (sm.r8(0x5ab93) == 0 || sm.r8(0x5aaa8) != 0) sub_3f284();
    cap.on = true;
    cap.snap_on = snap_enabled && !verify;
    sub_43306();
    if (int32_t(sm.r32(0x5ad5e)) > -0x8000000) {
      call_c(0x2fa6c);  // the two ground-layer passes over the object list
      call_c(0x2fa7e);
      sub_3c1bc();
      if (sm.r8(0x5aaa5) != 0) {
        sub_272e4();
      } else if (sub_3add8() != 0) {
        sub_272e4();
        sub_2fc14();
      } else {
        sub_2fc14();
        sub_272e4();
      }
    }
    cap.on = false;
    t_scene = Clock::now();
    if (overlay_enabled) {
      // The 2D drawing of this frame, twice: once on a cleared view and
      // once on a view of all ones.  A pixel that comes out the same was
      // drawn; one that keeps the fill is left to the 3D view.
      cap.stubs_on = false;
      sm.capture_image(before_2d);
      fill_view(0x00);
      draw_2d();
      read_planes(planes_a);
      sm.restore_image(before_2d);
      fill_view(0xFF);
      draw_2d();
      read_planes(out.overlay_index);
      cap.stubs_on = true;
      out.overlay_mask.assign(320 * 200, 0);
      for (size_t i = 0; i < out.overlay_index.size(); ++i) {
        const uint8_t a = planes_a[i], b = out.overlay_index[i];
        if (a == b) out.overlay_mask[i] = 255;
        else if ((a ^ b) != 15) {  // some planes kept: shown as drawn on index 0
          out.overlay_mask[i] = 255;
          out.overlay_index[i] = a;
          ++partial_pixels;
        }
      }
    } else {
      out.overlay_index.clear();
      out.overlay_mask.clear();
    }
  } catch (...) {
    cap.stubs_on = true;
    cap.on = false;
    failed = true;
    std::fprintf(stderr, "shadow: the scene pipeline did not return; new renderer disabled\n");
    return false;
  }

  t_2d = Clock::now();
  if (profile) {
    auto ms = [](Clock::time_point a, Clock::time_point b) {
      return std::chrono::duration<double, std::milli>(b - a).count();
    };
    prof[0] += ms(t_start, t_restore);
    prof[1] += ms(t_restore, t_scene);
    prof[2] += ms(t_scene, t_2d);
    if (++prof_n % prof_every == 0) {
      std::fprintf(stderr,
                   "shadow: image %zu KB; restore %.2f ms, scene %.2f ms, cockpit %.2f ms; "
                   "vertices exact %llu, as drawn %llu\n",
                   cur.image.ram.size() / 1024, prof[0] / double(prof_every), prof[1] / double(prof_every), prof[2] / double(prof_every),
                   (unsigned long long)cap.snapped, (unsigned long long)cap.unsnapped);
      cap.snapped = cap.unsnapped = 0;
      prof[0] = prof[1] = prof[2] = 0;
    }
  }
  // ---- display list ----
  CamState used = read_camera(sm);  // matrix and position the pipeline ran with
  M3 q;                             // quantised matrix as numbers
  for (int i = 0; i < 9; ++i) q.m[i] = double(used.m[i]) / 256.0;
  M3 exact;                         // exact row-scaled rotation
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) {
      double v = rot.m[r * 3 + c] * double(used.scale[r]) / 256.0;
      if (r == 0) v /= double(widen);
      exact.m[r * 3 + c] = v;
    }
  M3 qi;
  M3 refine;
  if (inverse(q, qi)) refine = mul(exact, qi);
  out.camera.external = used.external;
  out.camera.x = double(int64_t(used.sector[0]) << 22) - double(used.neg[0]);
  out.camera.y = -double(used.neg[1]);
  out.camera.z = double(int64_t(used.sector[1]) << 22) - double(used.neg[2]);
  for (int i = 0; i < 9; ++i) {
    out.camera.rot[size_t(i)] = float(rot.m[i]);
    out.refine[size_t(i)] = float(refine.m[i]);
  }
  for (int i = 0; i < 3; ++i) out.camera.scale[size_t(i)] = float(double(used.scale[i]) / 256.0);
  out.widen = widen;
  out.verts = cap.verts;
  out.prims = cap.prims;
  out.backdrop = sm.r8(0x5ab93) ? 6 : 4;
  out.water_all_planes = sm.r8(0x5aaa8) != 0;
  out.ground_altitude = terrain::scaled_altitude(int32_t(sm.r32(0x5ad70)), sm.r8(0x5aad5),
                                                 s16(sm.r16(0x5ad3a)));
  out.view_rows = std::min(200, int(s16(sm.r16(0x5ac7c))) + 1);
  out.game_frame = cur.image.frame;
  return true;
}

Shadow::Shadow() : p_(std::make_unique<Impl>()) {}
Shadow::~Shadow() = default;

bool Shadow::init(amiga::Machine& game, const std::string& disk_path, std::string& err) {
  Impl& s = *p_;
  s.game = &game;
  s.sm.quiet = true;
  s.sm.use_recomp = false;  // stubs must be reached; the scene code is native anyway
  s.sm.native_mode = game.native_mode == Machine::NativeMode::Off ? Machine::NativeMode::Off
                                                                   : Machine::NativeMode::On;
  s.sm.bypass_protection = game.bypass_protection;
  s.sm.set_det_timing(true);  // no emulated time passes on the shadow
  {
    MachineScope keep(native::g_machine);  // boot() makes itself the current machine
    if (!s.sm.boot(disk_path)) {
      err = "cannot boot the shadow machine from " + disk_path;
      return false;
    }
  }
  s.cap.install(s.sm, true);
  glr::set_object_observer([&s](uint32_t mesh, uint32_t, bool distant) {
    if (native::g_machine == &s.sm) s.cap.on_object(mesh, distant);
  });
  game.add_pc_hook(0x4217c, [&s] { s.on_view_setup(); });
  game.add_pc_hook(0x43306, [&s] { s.on_scene_start(); });
  if (std::getenv("FA18_SHADOW_VERIFY")) set_verify(true);
  return true;
}

void Shadow::set_verify(bool on) {
  Impl& s = *p_;
  if (on && !s.game_cap.m) {
    s.game_cap.install(*s.game, false);
    s.game->add_pc_hook(0x23d54, [&s] { s.on_scene_done(); });
  }
  s.verify = on;
}

uint64_t Shadow::serial() const { return p_->serial; }
uint64_t Shadow::newest_frame() const { return p_->cur.valid ? p_->cur.image.frame : 0; }
uint64_t Shadow::previous_frame() const {
  return p_->prev.valid ? p_->prev.image.frame : newest_frame();
}
bool Shadow::build(float alpha, float widen, Frame& out) { return p_->run(alpha, widen, out); }

}  // namespace render
