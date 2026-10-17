// Remaster: higher-resolution 3D view.
//
// The original draw routines keep running unchanged (game state stays
// bit-identical).  This module observes the scene primitives while the
// scene is drawn ($2fc14), re-renders them from the original 3D data at S
// times the resolution into a layer, and composes the layer with the
// low-resolution frame at scan-out: pixels the game drew *after* the scene
// (HUD, cockpit) are taken from the low-res frame.
//
// Primitive parameters (see $44b50 / $44686):
//   $5a9df       plane mask, bit b <-> colour bit b
//   $5a9e0 < 0   colour bits come from $5ac4c, else bits of $5a9e0 select
//                set (1) / clear (0); $5a9e2 bit b set = XOR instead
#include "amiga/machine.hpp"
#include "native/remaster.hpp"
#include "native/runtime.hpp"
#include "native/scene_frame.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace remaster {

namespace {
using native::rd16;
using native::rd32;
using native::rd8;

constexpr int W = 320;

struct PlaneOps {
  uint8_t set = 0, clr = 0, xr = 0;  // per display-plane bit masks
  int8_t cp_src = -1, cp_dst = -1;   // copy plane cp_src into cp_dst ($45014)
};

struct Layer {
  uint32_t base = 0;            // plane-0 pointer of the bitmap (key)
  int rows = 0;                 // view rows (low-res)
  int S = 1;
  std::vector<uint8_t> px;      // (W*S) x (rows*S) 4-bit indices
  std::vector<uint8_t> snap;    // W x rows, low-res 4-bit at scene end
  std::vector<uint8_t> s0;      // low-res 4-bit at scene start
  std::vector<uint8_t> cover;   // low-res cells touched by the hi-res renderer
  std::vector<uint8_t> unexplained;  // changed by the original, not by us
  uint64_t frame = 0;
  bool done = false;
  int prims = 0;                // primitives drawn so far this frame
};

std::vector<Layer> layers;      // small ring (double/triple buffering)
Layer* cur = nullptr;
bool in_scene = false;
bool gf_begun = false;    // layer was started by game_frame
void on_polygon();
bool pending_3d = false;  // a 3D polygon was rendered; its $44b50 fill follows
uint64_t stat_poly = 0, stat_line = 0;

// display plane p is the plane table entry 3-p (entry 3 is the lowest
// address = BPL1); mask/colour bit b drives display plane b
inline int plane_of_bit(int b) { return b; }

uint32_t plane_ptr(int display_plane) {
  uint32_t tbl = rd32(0x5a9ae);
  return rd32(tbl + 4u * uint32_t(3 - display_plane));
}

uint8_t low_index(const uint32_t pl[4], int x, int y) {
  uint8_t c = 0;
  uint32_t off = uint32_t(y * 40 + (x >> 3));
  for (int p = 0; p < 4; ++p)
    if (rd8(pl[p] + off) & (0x80 >> (x & 7))) c |= uint8_t(1 << p);
  return c;
}

PlaneOps poly_ops() {
  PlaneOps o;
  uint16_t mask = uint16_t(rd16(0x5a9de)) & 15;
  int16_t w1 = int16_t(rd16(0x5a9e0));
  if (w1 >= 0 && rd16(0x5a9e4) != 0) {
    // $45014: inside the polygon plane tbl[1] (display plane 2) := plane
    // tbl[0] (display plane 3)
    o.cp_src = 3;
    o.cp_dst = 2;
    return o;
  }
  uint16_t w2 = rd16(0x5a9e2);
  uint16_t col = rd16(0x5ac4c);
  for (int b = 0; b < 4; ++b) {
    if (!(mask & (1 << b))) continue;
    int pl = plane_of_bit(b);
    bool xr = (w2 >> b) & 1;
    if (w1 < 0) xr = w2 & 1;
    bool set = w1 < 0 ? ((col >> b) & 1) : ((uint16_t(w1) >> b) & 1);
    if (xr) o.xr |= uint8_t(1 << pl);
    else if (set) o.set |= uint8_t(1 << pl);
    else o.clr |= uint8_t(1 << pl);
  }
  return o;
}

PlaneOps line_ops() {
  PlaneOps o;
  uint16_t mask = uint16_t(rd16(0x5a9de)) & 15;
  int16_t w1 = int16_t(rd16(0x5a9e0));
  uint16_t col = rd16(0x5ac4c);
  for (int b = 0; b < 4; ++b) {
    if (!(mask & (1 << b))) continue;
    int pl = plane_of_bit(b);
    bool set = w1 < 0 ? ((col >> b) & 1) : ((uint16_t(w1) >> b) & 1);
    if (set) o.set |= uint8_t(1 << pl);
    else o.clr |= uint8_t(1 << pl);
  }
  return o;
}

inline void put(Layer& L, int x, int y, const PlaneOps& o) {
  uint8_t& p = L.px[size_t(y) * size_t(W * L.S) + size_t(x)];
  if (o.cp_src >= 0) p = uint8_t((p & ~(1 << o.cp_dst)) | (((p >> o.cp_src) & 1) << o.cp_dst));
  p = uint8_t(((p & ~o.clr) | o.set) ^ o.xr);
  L.cover[size_t(y / L.S) * W + size_t(x / L.S)] = 1;
}

// Resolve the flat colour a plane-op would write onto a black pixel, using
// the live COLOR registers.  Used to stamp Prim::rgba at capture time.
inline uint32_t rgba_from_ops(const PlaneOps& o) {
  if (o.cp_src >= 0) return 0;  // depends on destination pixel; unknown here
  uint8_t idx = uint8_t((0 & ~o.clr) | o.set) ^ o.xr;
  uint16_t c12 = native::g_machine->color_reg(idx & 31);
  uint32_t r = (c12 >> 8) & 0xF, g = (c12 >> 4) & 0xF, b = c12 & 0xF;
  r |= r << 4; g |= g << 4; b |= b << 4;
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}



// (Re)initialise the layer from the current low-res bitmap.  Called when the
// frame starts and again just before the first primitive: whatever the game
// did to the view in between (clear, background blits) is part of the base.
void init_from_bitmap(Layer& L) {
  const int S = L.S;
  uint32_t pl[4];
  for (int i = 0; i < 4; ++i) pl[i] = plane_ptr(i);
  L.px.assign(size_t(W * S) * size_t(L.rows * S), 0);
  L.s0.assign(size_t(W) * size_t(L.rows), 0);
  L.cover.assign(size_t(W) * size_t(L.rows), 0);
  L.unexplained.clear();
  for (int y = 0; y < L.rows; ++y)
    for (int x = 0; x < W; ++x) {
      uint8_t c = low_index(pl, x, y);
      L.s0[size_t(y) * W + size_t(x)] = c;
      for (int yy = 0; yy < S; ++yy)
        std::memset(&L.px[size_t(y * S + yy) * size_t(W * S) + size_t(x * S)], c, size_t(S));
    }
}

// first primitive of the frame: take the base now
inline bool first_prim() {
  if (!in_scene || !cur) return false;
  if (cur->prims++ == 0) init_from_bitmap(*cur);
  return true;
}

struct V3 { double x, y, z; };
struct V2 { double x, y; };

// view space -> screen (low-res units, x mirrored), see $39294
inline V2 project(const V3& v) { return {159.0 - 160.0 * v.x / v.z, 89.0 - 90.0 * v.y / v.z}; }

void draw_line(Layer& L, V2 a, V2 b, const PlaneOps& o);

void fill_polygon(Layer& L, const std::vector<V2>& pts, const PlaneOps& o) {
  const int S = L.S, Wp = W * S, Hp = L.rows * S;
  if (pts.size() < 3) return;
  double ymin = 1e30, ymax = -1e30, xmin = 1e30, xmax = -1e30;
  for (auto& p : pts) {
    ymin = std::min(ymin, p.y);
    ymax = std::max(ymax, p.y);
    xmin = std::min(xmin, p.x);
    xmax = std::max(xmax, p.x);
  }
  // slivers (cables, struts): the original's edge pixels keep them one
  // low-res pixel wide, so outline them
  if (std::min(xmax - xmin, ymax - ymin) < 1.5)
    for (size_t i = 0; i < pts.size(); ++i) draw_line(L, pts[i], pts[(i + 1) % pts.size()], o);
  int y0 = std::max(0, int(std::floor(ymin * S))), y1 = std::min(Hp - 1, int(std::ceil(ymax * S)));
  std::vector<double> xs;
  for (int y = y0; y <= y1; ++y) {
    double sy = (y + 0.5) / S;
    xs.clear();
    for (size_t i = 0; i < pts.size(); ++i) {
      const V2& a = pts[i];
      const V2& b = pts[(i + 1) % pts.size()];
      if ((a.y <= sy && b.y > sy) || (b.y <= sy && a.y > sy))
        xs.push_back(a.x + (sy - a.y) / (b.y - a.y) * (b.x - a.x));
    }
    std::sort(xs.begin(), xs.end());
    for (size_t k = 0; k + 1 < xs.size(); k += 2) {
      int xa = std::max(0, int(std::floor(xs[k] * S)));
      int xb = std::min(Wp - 1, int(std::ceil(xs[k + 1] * S)) - 1);
      for (int x = xa; x <= xb; ++x) put(L, x, y, o);
      // the original fills edge pixels our centre-sampling rule leaves out:
      // treat the neighbouring low-res cells as explained too
      if (xb >= xa) {
        int cy = y / S;
        for (int ry = std::max(0, cy - 1); ry <= std::min(L.rows - 1, cy + 1); ++ry)
          for (int cx = std::max(0, xa / S - 1); cx <= std::min(W - 1, xb / S + 1); ++cx)
            L.cover[size_t(ry) * W + size_t(cx)] = 1;
      }
    }
  }
}

void draw_line(Layer& L, V2 a, V2 b, const PlaneOps& o) {
  const int S = L.S, Wp = W * S, Hp = L.rows * S;
  double dx = (b.x - a.x) * S, dy = (b.y - a.y) * S;
  int n = int(std::ceil(std::max(std::fabs(dx), std::fabs(dy)))) + 1;
  for (int i = 0; i <= n; ++i) {
    double t = double(i) / n;
    double fx = (a.x + (b.x - a.x) * t) * S, fy = (a.y + (b.y - a.y) * t) * S;
    // a low-res pixel wide: S x S block anchored on the sample
    int px = int(std::floor(fx)), py = int(std::floor(fy));
    for (int ry = std::max(0, py / S - 1); ry <= std::min(L.rows - 1, py / S + 1); ++ry)
      for (int cx = std::max(0, px / S - 1); cx <= std::min(W - 1, px / S + 1); ++cx)
        L.cover[size_t(ry) * W + size_t(cx)] = 1;
    int bx = px - S / 2, by = py - S / 2;
    for (int yy = 0; yy < S; ++yy)
      for (int xx = 0; xx < S; ++xx) {
        int X = bx + xx, Y = by + yy;
        if (X >= 0 && X < Wp && Y >= 0 && Y < Hp) put(L, X, Y, o);
      }
  }
}

// clip a view-space polygon against z >= zmin
std::vector<V3> clip_near(const std::vector<V3>& in, double zmin) {
  std::vector<V3> out;
  for (size_t i = 0; i < in.size(); ++i) {
    const V3& a = in[i];
    const V3& b = in[(i + 1) % in.size()];
    bool ina = a.z >= zmin, inb = b.z >= zmin;
    if (ina) out.push_back(a);
    if (ina != inb) {
      double t = (zmin - a.z) / (b.z - a.z);
      out.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, zmin});
    }
  }
  return out;
}

bool skip_38c0c = false;  // $38c0a falls into $38c0c: handle a call once

void on_polygon_via_38c0a() {
  skip_38c0c = true;
  on_polygon();
}
void on_polygon_via_38c0c() {
  if (skip_38c0c) {
    skip_38c0c = false;
    return;
  }
  on_polygon();
}

void on_polygon() {
  pending_3d = false;
  int16_t shift = int16_t(rd16(0x61284));
  int n = int16_t(rd16(0x61286));
  if (n < 3 || n > 40) return;
  if (scene::enabled()) {
    scene::Prim pr;
    pr.kind = scene::PrimKind::Poly3d;
    pr.shift = int8_t(shift);
    pr.rgba = rgba_from_ops(poly_ops());
    int m = n > 16 ? 16 : n;
    pr.n = uint8_t(m);
    for (int i = 0; i < m; ++i) {
      uint32_t a = 0x61288 + 6u * uint32_t(i);
      pr.xyz[i][0] = int16_t(rd16(a));
      pr.xyz[i][1] = int16_t(rd16(a + 2));
      pr.xyz[i][2] = int16_t(rd16(a + 4));
    }
    scene::note_prim(pr);
  }
  if (!first_prim()) return;
  std::vector<V3> pts;
  for (int i = 0; i < n; ++i) {
    uint32_t a = 0x61288 + 6u * uint32_t(i);
    auto sh = [&](int16_t v) { return double(int16_t(uint32_t(int32_t(v)) << (shift & 31))); };
    pts.push_back({sh(int16_t(rd16(a))), sh(int16_t(rd16(a + 2))), sh(int16_t(rd16(a + 4)))});
  }
  auto clipped = clip_near(pts, 1.0);
  if (clipped.size() < 3) return;
  std::vector<V2> sp;
  for (auto& v : clipped) sp.push_back(project(v));
  fill_polygon(*cur, sp, poly_ops());
  pending_3d = true;
  ++stat_poly;
}

void on_fill2d() {
  // the fill of a 3D polygon ($38c0c calls $44b50 from $392cc) was rendered
  // from the 3D data already; anything else is a 2D polygon of its own
  if (rd32(native::g_machine->cpu.a[7]) == 0x392d2) return;
  int n = int16_t(rd16(0x60684));
  if (n < 3 || n > 60) return;
  // Screen-space fill (not a mesh).  Captured for SceneFrame; software
  // renderer leaves these to the low-res composite by default.
  if (scene::enabled()) {
    scene::Prim pr;
    pr.kind = scene::PrimKind::Fill2d;
    int m = n > 16 ? 16 : n;
    pr.n = uint8_t(m);
    pr.object_index = -1;
    pr.rgba = rgba_from_ops(poly_ops());
    for (int i = 0; i < m; ++i) {
      pr.xyz[i][0] = int16_t(rd16(0x60686 + 4u * uint32_t(i)));
      pr.xyz[i][1] = int16_t(rd16(0x60688 + 4u * uint32_t(i)));
      pr.xyz[i][2] = 0;
    }
    scene::note_prim(pr);
  }
  if (!first_prim()) return;
  std::vector<V2> sp;
  for (int i = 0; i < n; ++i)
    sp.push_back({double(int16_t(rd16(0x60686 + 4u * uint32_t(i)))) + 0.5,
                  double(int16_t(rd16(0x60688 + 4u * uint32_t(i)))) + 0.5});
  fill_polygon(*cur, sp, poly_ops());
}

// $44994 (horizon: sky/ground/sea planes) fills its screen-space polygon
// through $44dfe directly, not through $44b50: observe the return of $44dfe at $44b00
void on_fill_horizon() {
  pending_3d = false;
  // called at the return of $44dfe (bounds/edge set-up): d0 = 0 means the
  // polygon is visible and gets filled (bnes $44b14 otherwise).  The result
  // is read from d0, not from the flags (recompiled code keeps those lazily).
  if (native::g_machine->cpu.d[0] != 0) return;
  if (rd8(0x5ab93) == 0) return;            // no second colour: sky fill only
  int n = int16_t(rd16(0x60684));
  if (n < 3 || n > 60) return;
  // Horizon / sky / ground / sea — screen-space plane fills, not meshes.
  if (scene::enabled()) {
    scene::Prim pr;
    pr.kind = scene::PrimKind::Fill2d;
    int m = n > 16 ? 16 : n;
    pr.n = uint8_t(m);
    pr.object_index = -1;
    pr.sel = 1;  // horizon-class
    uint16_t c12 = native::g_machine->color_reg(0);
    uint32_t r = (c12 >> 8) & 0xF, g = (c12 >> 4) & 0xF, b = c12 & 0xF;
    r |= r << 4; g |= g << 4; b |= b << 4;
    pr.rgba = 0xFF000000u | (r << 16) | (g << 8) | b;
    for (int i = 0; i < m; ++i) {
      pr.xyz[i][0] = int16_t(rd16(0x60686 + 4u * uint32_t(i)));
      pr.xyz[i][1] = int16_t(rd16(0x60688 + 4u * uint32_t(i)));
      pr.xyz[i][2] = 0;
    }
    scene::note_prim(pr);
  }
  if (!first_prim()) return;
  std::vector<V2> sp;
  for (int i = 0; i < n; ++i)
    sp.push_back({double(int16_t(rd16(0x60686 + 4u * uint32_t(i)))) + 0.5,
                  double(int16_t(rd16(0x60688 + 4u * uint32_t(i)))) + 0.5});
  PlaneOps o;
  o.clr = 1 << 1;  // $4506e with d0=8, d3=0: clear display plane 1 inside the polygon
  fill_polygon(*cur, sp, o);
}

// $4397c: line between two points that must lie inside the frustum (no clipping)
void on_line_noclip() {
  V3 p[2];
  for (int i = 0; i < 2; ++i) {
    uint32_t a = 0x61886 + 6u * uint32_t(i);
    p[i] = {double(int16_t(rd16(a))), double(int16_t(rd16(a + 2))), double(int16_t(rd16(a + 4)))};
    if (p[i].z <= 0 || std::fabs(p[i].x) > p[i].z || std::fabs(p[i].y) > p[i].z) return;
  }
  if (scene::enabled()) {
    scene::Prim pr;
    pr.kind = scene::PrimKind::Line;
    pr.n = 2;
    pr.rgba = rgba_from_ops(line_ops());
    for (int i = 0; i < 2; ++i) {
      pr.xyz[i][0] = int16_t(p[i].x);
      pr.xyz[i][1] = int16_t(p[i].y);
      pr.xyz[i][2] = int16_t(p[i].z);
    }
    scene::note_prim(pr);
  }
  if (!first_prim()) return;
  draw_line(*cur, project(p[0]), project(p[1]), line_ops());
}

// Point primitives ($438a0 and variants): d0-d2 = view-space x,y,z, d7 = size
// selector.  Sizes: 1x1 ($441fc), 2x1 ($44212), 2x2 ($44276), disc of
// radius d6 ($43dcc).  `sel` is the value the entry point loads into d7
// (INT_MIN: use $5adb0 like $438a0).
void fill_rect(Layer& L, double cx, double cy, double w, double h, const PlaneOps& o) {
  const int S = L.S, Wp = W * S, Hp = L.rows * S;
  int x0 = int(std::floor((cx - w / 2) * S + 0.5)), x1 = int(std::floor((cx + w / 2) * S + 0.5));
  int y0 = int(std::floor((cy - h / 2) * S + 0.5)), y1 = int(std::floor((cy + h / 2) * S + 0.5));
  if (x1 <= x0) x1 = x0 + 1;
  if (y1 <= y0) y1 = y0 + 1;
  for (int y = std::max(0, y0); y < std::min(Hp, y1); ++y)
    for (int x = std::max(0, x0); x < std::min(Wp, x1); ++x) put(L, x, y, o);
}

void on_point(int sel) {
  pending_3d = false;
  m68k::Cpu& c = native::g_machine->cpu;
  int16_t x = int16_t(c.d[0]), y = int16_t(c.d[1]), z = int16_t(c.d[2]);
  if (z <= 0 || std::abs(int(x)) >= z || std::abs(int(y)) >= z) return;
  int d7 = sel == INT32_MIN ? int16_t(rd16(0x5adb0)) : sel;
  if (scene::enabled()) {
    scene::Prim pr;
    pr.kind = scene::PrimKind::Point;
    pr.n = 1;
    pr.sel = int16_t(d7);
    pr.rgba = rgba_from_ops(line_ops());
    pr.xyz[0][0] = x;
    pr.xyz[0][1] = y;
    pr.xyz[0][2] = z;
    scene::note_prim(pr);
  }
  if (!first_prim()) return;
  V2 sp = project({double(x), double(y), double(z)});
  // low-res pixel centre of the projected position (the original rounds
  // to integer screen coordinates; +1 row as at $43902)
  double cx = sp.x + 0.5, cy = sp.y + 0.5 + 1.0;
  int kind;  // 0: 1x1, 1: 2x1, 2: 2x2, 3: disc
  if (d7 >= 0) {
    int limit = int16_t(rd16(c.a[6] - 40));
    if ((48 >> d7) >= limit) kind = 2;
    else if ((80 >> d7) >= limit) kind = 1;
    else kind = 0;
  } else if (d7 == -1) kind = 0;
  else if (d7 == -2) kind = 1;
  else if (d7 == -3) kind = 2;
  else if (d7 == -4) kind = 3;
  else return;
  PlaneOps o = line_ops();
  Layer& L = *cur;
  if (kind == 0) fill_rect(L, cx, cy, 1, 1, o);
  else if (kind == 1) fill_rect(L, cx - 0.5, cy, 2, 1, o);
  else if (kind == 2) fill_rect(L, cx, cy, 2, 2, o);
  else {
    int r = std::min<int>(127, int16_t(c.d[6]));
    if (r < 1) r = 1;
    std::vector<V2> circ;
    for (int i = 0; i < 48; ++i) {
      double a = 2 * M_PI * i / 48;
      circ.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
    }
    fill_polygon(L, circ, poly_ops());
  }
}

void on_line() {
  V3 a{double(int16_t(rd16(0x61886))), double(int16_t(rd16(0x61888))), double(int16_t(rd16(0x6188a)))};
  V3 b{double(int16_t(rd16(0x6188c))), double(int16_t(rd16(0x6188e))), double(int16_t(rd16(0x61890)))};
  auto c = clip_near({a, b}, 1.0);
  if (c.size() != 2) return;
  if (scene::enabled()) {
    scene::Prim pr;
    pr.kind = scene::PrimKind::Line;
    pr.n = 2;
    pr.rgba = rgba_from_ops(line_ops());
    for (int i = 0; i < 2; ++i) {
      pr.xyz[i][0] = int16_t(c[i].x);
      pr.xyz[i][1] = int16_t(c[i].y);
      pr.xyz[i][2] = int16_t(c[i].z);
    }
    scene::note_prim(pr);
  }
  if (!first_prim()) return;
  draw_line(*cur, project(c[0]), project(c[1]), line_ops());
  ++stat_line;
}
}  // namespace

bool scene_hooks = false;

void reset() {
  layers.clear();
  gf_begun = false;
  cur = nullptr;
  in_scene = false;
  pending_3d = false;
}

static void do_begin();

// the layer starts at the top of game_frame ($2112c) so that the sky and
// ground drawn before $2fc14 are part of it; $2fc14 must not restart it
static void on_game_frame() {
  do_begin();
  gf_begun = in_scene;
}

void scene_begin() {
  if (gf_begun && in_scene) {
    gf_begun = false;
    return;
  }
  do_begin();
}

static void do_begin() {
  amiga::Machine& m = *native::g_machine;
  // Open a SceneFrame at game_frame so horizon / sky drawn before $2fc14
  // are attributed to this frame (begin_frame is idempotent per frame_id).
  if (scene::enabled()) scene::begin_frame(m.frame_count());
  int S = m.hires_scale;
  if (S <= 0) return;
  uint32_t pl[4];
  for (int i = 0; i < 4; ++i) pl[i] = plane_ptr(i);
  uint32_t base = pl[0];
  Layer* L = nullptr;
  for (auto& l : layers) if (l.base == base) L = &l;
  if (!L) {
    if (layers.size() < 4) { layers.emplace_back(); L = &layers.back(); }
    else {
      L = &*std::min_element(layers.begin(), layers.end(),
                             [](const Layer& a, const Layer& b) { return a.frame < b.frame; });
    }
  }
  L->base = base;
  L->S = S;
  L->rows = std::min(200, int(rd16(0x5ac7c)) + 1);
  L->frame = m.frame_count();
  L->done = false;
  L->prims = 0;
  init_from_bitmap(*L);
  cur = L;
  in_scene = true;
}

void scene_end() {
  if (!in_scene || !cur) return;
  in_scene = false;
  uint32_t pl[4];
  for (int i = 0; i < 4; ++i) pl[i] = plane_ptr(i);
  cur->snap.assign(size_t(W) * size_t(cur->rows), 0);
  for (int y = 0; y < cur->rows; ++y)
    for (int x = 0; x < W; ++x) cur->snap[size_t(y) * W + size_t(x)] = low_index(pl, x, y);
  cur->unexplained.assign(cur->snap.size(), 0);
  for (size_t i = 0; i < cur->snap.size(); ++i)
    cur->unexplained[i] = (cur->snap[i] != cur->s0[i]) && !cur->cover[i];
  cur->done = true;
  if (getenv("FA18_HIRES_STATS")) {
    int u = 0, ch = 0, ex = 0;
    for (size_t i = 0; i < cur->snap.size(); ++i) {
      u += cur->unexplained[i];
      ch += cur->snap[i] != cur->s0[i];
      ex += (cur->snap[i] != cur->s0[i]) && cur->cover[i];
    }
    std::fprintf(stderr, "[hires] frame %llu changed=%d explained=%d unexplained=%d\n",
                 (unsigned long long)native::g_machine->frame_count(), ch, ex, u);
  }
}

void install(amiga::Machine& m) {
  scene_hooks = true;
  uint32_t dummy = 0;
  (void)dummy;
  auto poly = [] { on_polygon(); };
  // $38c0a falls into $38c0c: observe both entries, but only count once
  m.add_pc_hook(0x2112c, [] { on_game_frame(); });
  m.add_pc_hook(0x38c0c, poly);
  m.add_pc_hook(0x38c0a, poly);
  m.add_pc_hook(0x43a56, [] { on_line(); });
  m.add_pc_hook(0x4397c, [] { on_line_noclip(); });
  m.add_pc_hook(0x44b50, [] { on_fill2d(); });
  m.add_pc_hook(0x44b00, [] { on_fill_horizon(); });
  // dots/discs and other direct fill callers: reset the pairing flag
  m.add_pc_hook(0x438a0, [] { on_point(INT32_MIN); });
  m.add_pc_hook(0x438a8, [] { on_point(-4); });
  m.add_pc_hook(0x438ac, [] { on_point(-3); });
  m.add_pc_hook(0x438b0, [] { on_point(-2); });
  m.add_pc_hook(0x438b4, [] { on_point(-1); });
  m.add_pc_hook(0x4389c, [] { on_point(-5); });
}

bool compose(amiga::Machine& m, std::vector<uint32_t>& out) {
  const int S = m.hires_scale;
  if (S <= 0) return false;
  const int Wp = W * S, Hp = 200 * S;
  out.resize(size_t(Wp) * size_t(Hp));
  const uint32_t* rgb = m.frame_rgb();
  const uint8_t* idx = m.frame_idx();
  const uint32_t* key = m.frame_row_key();
  const uint32_t* pal = m.frame_row_pal();
  if (!idx || !key) return false;
  if (getenv("FA18_HIRES_KEYS") && m.frame_count() % 400 == 0) {
    std::fprintf(stderr, "[hires] frame %llu key0=%06x key50=%06x key100=%06x layers:", (unsigned long long)m.frame_count(), key[0], key[50], key[100]);
    for (auto& l : layers) std::fprintf(stderr, " %06x(rows=%d done=%d)", l.base, l.rows, l.done);
    std::fprintf(stderr, "\n");
  }
  int mismatch = 0;
  for (int y = 0; y < 200; ++y) {
    // find the layer this row belongs to
    const Layer* L = nullptr;
    int ly = -1;
    for (auto& l : layers) {
      if (!l.done || key[y] < l.base) continue;
      uint32_t d = key[y] - l.base;
      if (d % 40 != 0) continue;
      int row = int(d / 40);
      if (row < l.rows) { L = &l; ly = row; break; }
    }
    for (int yy = 0; yy < S; ++yy) {
      uint32_t* o = &out[size_t(y * S + yy) * size_t(Wp)];
      for (int x = 0; x < W; ++x) {
        uint32_t c = rgb[y * W + x];
        if (L) {
          uint8_t li = idx[y * W + x];
          size_t ci = size_t(ly) * W + size_t(x);
          if ((li & 15) == L->snap[ci] && !L->unexplained[ci]) {
            const uint8_t* hp = &L->px[size_t(ly * S + yy) * size_t(Wp) + size_t(x * S)];
            for (int xx = 0; xx < S; ++xx) {
              uint32_t hc = pal[size_t(y) * 32 + size_t((hp[xx] & 15) | (li & 16))];
              o[x * S + xx] = hc;
              if (S == 1 && hc != c) ++mismatch;
            }
            continue;
          }
        }
        if (L && getenv("FA18_HIRES_DEBUG")) c = 0xFF00FF;
        for (int xx = 0; xx < S; ++xx) o[x * S + xx] = c;
      }
    }
  }
  if (S == 1 && getenv("FA18_HIRES_STATS"))
    std::fprintf(stderr, "[hires-diff] frame %llu mismatch=%d\n", (unsigned long long)m.frame_count(), mismatch);
  return true;
}

}  // namespace remaster
