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

#include <algorithm>
#include <cmath>
#include <cstdio>
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
};

std::vector<Layer> layers;      // small ring (double/triple buffering)
Layer* cur = nullptr;
bool in_scene = false;
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
  p = uint8_t(((p & ~o.clr) | o.set) ^ o.xr);
  L.cover[size_t(y / L.S) * W + size_t(x / L.S)] = 1;
}

struct V3 { double x, y, z; };
struct V2 { double x, y; };

// view space -> screen (low-res units, x mirrored), see $39294
inline V2 project(const V3& v) { return {159.0 - 160.0 * v.x / v.z, 89.0 - 90.0 * v.y / v.z}; }

void fill_polygon(Layer& L, const std::vector<V2>& pts, const PlaneOps& o) {
  const int S = L.S, Wp = W * S, Hp = L.rows * S;
  if (pts.size() < 3) return;
  double ymin = 1e30, ymax = -1e30;
  for (auto& p : pts) { ymin = std::min(ymin, p.y); ymax = std::max(ymax, p.y); }
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

void on_polygon() {
  pending_3d = false;
  if (!in_scene || !cur) return;
  int16_t shift = int16_t(rd16(0x61284));
  int n = int16_t(rd16(0x61286));
  if (n < 3 || n > 40) return;
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
  if (pending_3d) { pending_3d = false; return; }
  if (!in_scene || !cur) return;
  int n = int16_t(rd16(0x60684));
  if (n < 3 || n > 60) return;
  std::vector<V2> sp;
  for (int i = 0; i < n; ++i)
    sp.push_back({double(int16_t(rd16(0x60686 + 4u * uint32_t(i)))) + 0.5,
                  double(int16_t(rd16(0x60688 + 4u * uint32_t(i)))) + 0.5});
  fill_polygon(*cur, sp, poly_ops());
}

// $4397c: line between two points that must lie inside the frustum (no clipping)
void on_line_noclip() {
  if (!in_scene || !cur) return;
  V3 p[2];
  for (int i = 0; i < 2; ++i) {
    uint32_t a = 0x61886 + 6u * uint32_t(i);
    p[i] = {double(int16_t(rd16(a))), double(int16_t(rd16(a + 2))), double(int16_t(rd16(a + 4)))};
    if (p[i].z <= 0 || std::fabs(p[i].x) > p[i].z || std::fabs(p[i].y) > p[i].z) return;
  }
  draw_line(*cur, project(p[0]), project(p[1]), line_ops());
}

void on_line() {
  if (!in_scene || !cur) return;
  V3 a{double(int16_t(rd16(0x61886))), double(int16_t(rd16(0x61888))), double(int16_t(rd16(0x6188a)))};
  V3 b{double(int16_t(rd16(0x6188c))), double(int16_t(rd16(0x6188e))), double(int16_t(rd16(0x61890)))};
  auto c = clip_near({a, b}, 1.0);
  if (c.size() != 2) return;
  draw_line(*cur, project(c[0]), project(c[1]), line_ops());
  ++stat_line;
}
}  // namespace

bool scene_hooks = false;

void scene_begin() {
  amiga::Machine& m = *native::g_machine;
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
  L->px.assign(size_t(W * S) * size_t(L->rows * S), 0);
  L->s0.assign(size_t(W) * size_t(L->rows), 0);
  L->cover.assign(size_t(W) * size_t(L->rows), 0);
  L->unexplained.clear();
  for (int y = 0; y < L->rows; ++y)
    for (int x = 0; x < W; ++x) {
      uint8_t c = low_index(pl, x, y);
      L->s0[size_t(y) * W + size_t(x)] = c;
      for (int yy = 0; yy < S; ++yy)
        std::memset(&L->px[size_t(y * S + yy) * size_t(W * S) + size_t(x * S)], c, size_t(S));
    }
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
}

void install(amiga::Machine& m) {
  scene_hooks = true;
  uint32_t dummy = 0;
  (void)dummy;
  auto poly = [] { on_polygon(); };
  // $38c0a falls into $38c0c: observe both entries, but only count once
  m.add_pc_hook(0x38c0c, poly);
  m.add_pc_hook(0x38c0a, poly);
  m.add_pc_hook(0x43a56, [] { on_line(); });
  m.add_pc_hook(0x4397c, [] { on_line_noclip(); });
  m.add_pc_hook(0x44b50, [] { on_fill2d(); });
  // dots/discs and other direct fill callers: reset the pairing flag
  for (uint32_t a : {0x438a0u, 0x438a8u, 0x438acu, 0x438b0u, 0x438b4u, 0x4389cu, 0x43dccu})
    m.add_pc_hook(a, [] { pending_3d = false; });
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
            for (int xx = 0; xx < S; ++xx) o[x * S + xx] = pal[size_t(y) * 32 + size_t((hp[xx] & 15) | (li & 16))];
            continue;
          }
        }
        if (L && getenv("FA18_HIRES_DEBUG")) c = 0xFF00FF;
        for (int xx = 0; xx < S; ++xx) o[x * S + xx] = c;
      }
    }
  }
  return true;
}

}  // namespace remaster
