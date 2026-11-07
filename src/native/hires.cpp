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
//   $5a9e0 < 0   polygon colour bits come from $5ac4c, line bits from
//                $5ac4f (low byte of $5ac4e); else bits of $5a9e0 select
//                set (1) / clear (0); $5a9e2 bit b set = XOR for polygons
#include "amiga/machine.hpp"
#include "native/gl_renderer.hpp"
#include "native/hud_erase.hpp"
#include "native/hud_glyph.hpp"
#include "native/cockpit_rail.hpp"
#include "native/cockpit_front.hpp"
#include "native/cockpit_panel.hpp"
#include "native/cockpit_dynamic_audit.hpp"
#include "native/gl_scene_rows.hpp"
#include "native/cockpit_assets.hpp"
#include "native/cockpit_instruments.hpp"
#include "native/remaster.hpp"
#include "native/runtime.hpp"
#include "native/scene_frame.hpp"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace remaster {

static int hud_backdrop_pen(int y, int x);

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
  bool gl_cached = false;
  int prims = 0;                // primitives drawn so far this frame
};

std::vector<Layer> layers;      // small ring (double/triple buffering)
Layer* cur = nullptr;
bool in_scene = false;
bool gf_begun = false;    // layer was started by game_frame
void on_polygon();
bool pending_3d = false;  // a 3D polygon was rendered; its $44b50 fill follows
NativeHudState last_prepared_gpu_hud;
// The original 68000 $2fc14 scene routine returns to these caller PCs.
// Interpreted execution needs its own end hook; the native C++ wrapper
// already invokes scene_end() itself.
std::vector<uint32_t> scene_return_pcs;
uint64_t stat_poly = 0, stat_line = 0;

// In GL mode, the original high-resolution software colour buffer is never
// displayed: draw_layer() replaces it with the completed GPU scene.  The
// coverage mask still matters because it distinguishes captured 3D drawing
// from otherwise unexplained original 2D pixels. Keep the legacy path
// available for a deterministic mask/pixel comparison.
bool coverage_only() {
  static const bool enabled = [] {
    const char* reference = std::getenv("FA18_GL_COVERAGE_REFERENCE");
    return !reference || std::strcmp(reference, "0") == 0;
  }();
  return enabled && glr::active();
}

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
  // $44782: the negative-mode line emitter tests $5ac4f, not the polygon
  // colour word at $5ac4c (verified against the original ADF instructions).
  uint16_t col = rd16(0x5ac4e);
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

// Resolve a flat colour from plane-ops + live COLOR registers for Prim::rgba.
// Copy-plane ops ($45014) depend on the destination pixel; approximate with
// the current $5ac4c colour word so the software renderer still gets a tint.
inline glr::Ops gl_ops(const PlaneOps& o) {
  glr::Ops g;
  g.set = o.set;
  g.clr = o.clr;
  g.xr = o.xr;
  g.copy = o.cp_src >= 0;
  return g;
}

inline uint32_t rgba_from_ops(const PlaneOps& o) {
  uint8_t idx;
  if (o.cp_src >= 0) {
    uint16_t col = rd16(0x5ac4c);
    idx = uint8_t(col & 15);
  } else {
    idx = uint8_t((0 & ~o.clr) | o.set) ^ o.xr;
  }
  uint16_t c12 = native::g_machine->color_reg(idx & 31);
  uint32_t r = (c12 >> 8) & 0xF, g = (c12 >> 4) & 0xF, b = c12 & 0xF;
  r |= r << 4; g |= g << 4; b |= b << 4;
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}



// (Re)initialise the layer from the current low-res bitmap.  Called when the
// frame starts and again just before the first primitive: whatever the game
// did to the view in between (clear, background blits) is part of the base.
void init_from_bitmap(Layer& L) {
  amiga::Machine::RasterAuditScope audit(*native::g_machine,
                                        amiga::Machine::RasterSource::Presenter);
  const int S = L.S;
  uint32_t pl[4];
  for (int i = 0; i < 4; ++i) pl[i] = plane_ptr(i);
  // With the OpenGL renderer the layer pixels are replaced at the end of
  // the scene: only the low-res base and the coverage are needed.
  const bool fill_px = !coverage_only();
  if (fill_px) L.px.resize(size_t(W * S) * size_t(L.rows * S));
  else L.px.clear();
  L.s0.resize(size_t(W) * size_t(L.rows));
  L.cover.assign(size_t(W) * size_t(L.rows), 0);
  L.unexplained.clear();
  std::vector<uint8_t> wide;
  if (fill_px) wide.resize(size_t(W * S));
  for (int y = 0; y < L.rows; ++y) {
    uint8_t* row = &L.s0[size_t(y) * W];
    const uint32_t off = uint32_t(y * 40);
    for (int bx = 0; bx < W / 8; ++bx) {
      uint8_t b[4];
      for (int p = 0; p < 4; ++p) b[p] = rd8(pl[p] + off + uint32_t(bx));
      for (int i = 0; i < 8; ++i) {
        const int sh = 7 - i;
        row[bx * 8 + i] = uint8_t(((b[0] >> sh) & 1) | (((b[1] >> sh) & 1) << 1) |
                                  (((b[2] >> sh) & 1) << 2) | (((b[3] >> sh) & 1) << 3));
      }
    }
    if (!fill_px) continue;
    for (int x = 0; x < W; ++x) std::memset(&wide[size_t(x * S)], row[x], size_t(S));
    for (int yy = 0; yy < S; ++yy)
      std::memcpy(&L.px[size_t(y * S + yy) * size_t(W * S)], wide.data(), wide.size());
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
  const bool mask_only = coverage_only();
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
      if (!mask_only)
        for (int x = xa; x <= xb; ++x) put(L, x, y, o);
      // the original fills edge pixels our centre-sampling rule leaves out:
      // treat the neighbouring low-res cells as explained too
      if (xb >= xa) {
        int cy = y / S;
        for (int ry = std::max(0, cy - 1); ry <= std::min(L.rows - 1, cy + 1); ++ry)
          std::fill_n(&L.cover[size_t(ry) * W + size_t(std::max(0, xa / S - 1))],
                      size_t(std::min(W - 1, xb / S + 1) - std::max(0, xa / S - 1) + 1),
                      uint8_t(1));
      }
    }
  }
}

void draw_line(Layer& L, V2 a, V2 b, const PlaneOps& o) {
  const int S = L.S, Wp = W * S, Hp = L.rows * S;
  const bool mask_only = coverage_only();
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
    if (mask_only) continue;
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
  if (glr::active()) {
    int16_t v[40][3];
    for (int i = 0; i < n; ++i)
      for (int k = 0; k < 3; ++k) v[i][k] = int16_t(rd16(0x61288 + 6u * uint32_t(i) + 2u * uint32_t(k)));
    glr::note_poly(v, n, shift, gl_ops(poly_ops()));
  }
  if (scene::enabled()) {
    scene::Prim pr;
    pr.kind = scene::PrimKind::Poly3d;
    pr.shift = int8_t(shift);
    pr.rgba = rgba_from_ops(poly_ops());
    int m = n > 40 ? 40 : n;
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
    int m = n > 40 ? 40 : n;
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

// Horizon geometry (docs/horizon.md):
//   Complex path: $42f6a/$42f62 transforms a finite sea quad, $43364 clips,
//   then rebuilds $60684 as count-prefixed *screen-space* verts.  Capture at
//   the successful RTS ($232aa) when that layout is valid.
//   Simple path: $44994 (bit13 of $5abca clear) is a full-view plane-1 clear
//   with no polygon — emit a view-rectangle Fill2d instead.
//
// $60684 layout after rebuild: word n; n × (word x, word y).

static uint32_t horizon_rgba12(int idx) {
  uint16_t c12 = native::g_machine->color_reg(idx & 31);
  uint32_t r = (c12 >> 8) & 0xF, g = (c12 >> 4) & 0xF, b = c12 & 0xF;
  r |= r << 4; g |= g << 4; b |= b << 4;
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}

// Resolve sky / ground colours and inject a full-view sky prim once per frame.
static void horizon_colours(bool ground, uint32_t& sky_out, uint32_t& ground_out) {
  sky_out = horizon_rgba12(0);
  if ((sky_out & 0xFFFFFF) == 0) sky_out = 0xFF5080C0u;
  ground_out = 0xFF206080u;
  if (ground) {
    int best_score = -1;
    for (int idx : {2, 3, 6, 7, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15}) {
      uint32_t c = horizon_rgba12(idx);
      if ((c & 0xFFFFFF) == 0) continue;
      int dr = int((c >> 16) & 255) - int((sky_out >> 16) & 255);
      int dg = int((c >> 8) & 255) - int((sky_out >> 8) & 255);
      int db = int(c & 255) - int(sky_out & 255);
      int score = dr * dr + dg * dg + db * db;
      int blue_bias = int(c & 255) - int((c >> 16) & 255);
      if (blue_bias > 0) score += blue_bias * blue_bias;
      if (score > best_score) { best_score = score; ground_out = c; }
    }
  }
  scene::note_env_colours(sky_out, ground ? ground_out : 0);
  static uint64_t sky_injected_for = ~0ull;
  uint64_t fid = scene::current().frame_id;
  if (sky_injected_for != fid) {
    sky_injected_for = fid;
    scene::Prim sky_pr;
    sky_pr.kind = scene::PrimKind::Fill2d;
    sky_pr.n = 4;
    sky_pr.object_index = -1;
    sky_pr.sel = 2;
    sky_pr.rgba = sky_out;
    int rows = std::min(200, int(rd16(0x5ac7c)) + 1);
    sky_pr.xyz[0][0] = 0;   sky_pr.xyz[0][1] = 0;
    sky_pr.xyz[1][0] = 320; sky_pr.xyz[1][1] = 0;
    sky_pr.xyz[2][0] = 320; sky_pr.xyz[2][1] = int16_t(rows);
    sky_pr.xyz[3][0] = 0;   sky_pr.xyz[3][1] = int16_t(rows);
    scene::note_prim(sky_pr);
  }
}

// Capture the count-prefixed screen-space polygon at $60684 (complex path).
void capture_horizon_fill() {
  int n = int16_t(rd16(0x60684));
  if (n < 3 || n > 60) return;
  if (!scene::enabled()) {
    if (rd8(0x5ab93) == 0) return;
    if (!first_prim()) return;
    // fall through to hires-only path below
  } else {
    // $60684 poly from $42f6a is always the sea/ground region (screen-space).
    // $5ab93 only gates the second-colour blitter fill in the original; for
    // the software renderer we still need the shape as sel=1 (ground).
    scene::Prim pr;
    pr.kind = scene::PrimKind::Fill2d;
    int m = n > 40 ? 40 : n;
    pr.n = uint8_t(m);
    pr.object_index = -1;
    pr.sel = 1;
    uint32_t sky = 0, gr = 0;
    horizon_colours(/*ground=*/true, sky, gr);
    pr.rgba = gr;
    for (int i = 0; i < m; ++i) {
      pr.xyz[i][0] = int16_t(rd16(0x60686 + 4u * uint32_t(i)));
      pr.xyz[i][1] = int16_t(rd16(0x60688 + 4u * uint32_t(i)));
      pr.xyz[i][2] = 0;
    }
    if (std::getenv("FA18_HORIZON_DEBUG")) {
      std::fprintf(stderr, "[horizon] capture n=%d rgba=%08x verts:", m, pr.rgba);
      for (int i = 0; i < m && i < 8; ++i)
        std::fprintf(stderr, " (%d,%d)", int(pr.xyz[i][0]), int(pr.xyz[i][1]));
      std::fprintf(stderr, "\n");
    }
    scene::note_prim(pr);
  }
  if (rd8(0x5ab93) == 0) return;            // hires: no second colour, sky only
  if (!first_prim()) return;
  std::vector<V2> sp;
  for (int i = 0; i < n; ++i)
    sp.push_back({double(int16_t(rd16(0x60686 + 4u * uint32_t(i)))) + 0.5,
                  double(int16_t(rd16(0x60688 + 4u * uint32_t(i)))) + 0.5});
  PlaneOps o;
  o.clr = 1 << 1;  // $4506e with d0=8, d3=0: clear display plane 1 inside the polygon
  fill_polygon(*cur, sp, o);
}

void on_fill_horizon() {
  // $44b00: after $44dfe setup.  d0==0 means the polygon is visible.
  if (native::g_machine->cpu.d[0] != 0) return;
  capture_horizon_fill();
}

// $44f46: fill used by both the complex path (after $42f6a rebuilt $60684)
// and the simple $44994 plane-clear.  Capture the poly when present; if the
// count at $60684 is not a valid polygon the capture is a no-op.
void on_fill_44f46() {
  m68k::Cpu& c = native::g_machine->cpu;
  if (int16_t(c.d[0]) != 8) return;
  if (int16_t(c.d[3]) != 0) return;
  capture_horizon_fill();
}

// Successful RTS of $42f6a/$42f62 body ($232aa): $60684 holds the rebuilt
// count-prefixed screen-space sea polygon (docs/horizon.md).
void on_horizon_poly_ready() {
  if (!scene::enabled()) return;
  int n = int16_t(rd16(0x60684));
  if (std::getenv("FA18_HORIZON_DEBUG"))
    std::fprintf(stderr, "[horizon] poly_ready n=%d ab93=%d camY=%d\n",
                 n, int(rd8(0x5ab93)), int32_t(rd32(0x5ad5e)));
  capture_horizon_fill();
}

// Failure RTS of $42f6a ($432b6): clip produced no usable sea poly.
void on_horizon_poly_fail() {
  if (!std::getenv("FA18_HORIZON_DEBUG")) return;
  // $63b44: 4 edge flags (words) + coords
  std::fprintf(stderr, "[horizon] FAIL camY=%d flags=%d,%d,%d,%d\n",
               int32_t(rd32(0x5ad5e)),
               int16_t(rd16(0x63b44)), int16_t(rd16(0x63b46)),
               int16_t(rd16(0x63b48)), int16_t(rd16(0x63b4a)));
  // View-space corners written by $42f6a (stride 32 bytes, 3 words each)
  for (int i = 0; i < 4; ++i) {
    uint32_t a = 0x60684 + uint32_t(i) * 32;
    std::fprintf(stderr, "  corner%d view=(%d,%d,%d)\n", i,
                 int16_t(rd16(a)), int16_t(rd16(a+2)), int16_t(rd16(a+4)));
  }
}

// Simple horizon path entry ($44994): full-view plane clears + optional sea poly.
// Emit a view-rectangle Fill2d so the software renderer still has sea/sky.
void on_simple_horizon() {
  if (!scene::enabled()) return;
  // Only the simple path: bit13 of $5abca clear (see $42f48).
  uint16_t flags = rd16(0x5abca);
  if (std::getenv("FA18_HORIZON_DEBUG"))
    std::fprintf(stderr, "[horizon] simple abca=%04x ab93=%d\n", flags, int(rd8(0x5ab93)));
  if (flags & 0x2000) return;
  const bool ground = rd8(0x5ab93) != 0;
  uint32_t sky = 0, gr = 0;
  horizon_colours(ground, sky, gr);
  if (!ground) return;  // sky already injected by horizon_colours
  scene::Prim pr;
  pr.kind = scene::PrimKind::Fill2d;
  pr.n = 4;
  pr.object_index = -1;
  pr.sel = 1;
  pr.rgba = gr;
  int rows = std::min(200, int(rd16(0x5ac7c)) + 1);
  pr.xyz[0][0] = 0;   pr.xyz[0][1] = 0;
  pr.xyz[1][0] = 320; pr.xyz[1][1] = 0;
  pr.xyz[2][0] = 320; pr.xyz[2][1] = int16_t(rows);
  pr.xyz[3][0] = 0;   pr.xyz[3][1] = int16_t(rows);
  scene::note_prim(pr);
}

// $4397c: line between two points that must lie inside the frustum (no clipping)
void on_line_noclip() {
  V3 p[2];
  for (int i = 0; i < 2; ++i) {
    uint32_t a = 0x61886 + 6u * uint32_t(i);
    p[i] = {double(int16_t(rd16(a))), double(int16_t(rd16(a + 2))), double(int16_t(rd16(a + 4)))};
    if (p[i].z <= 0 || std::fabs(p[i].x) > p[i].z || std::fabs(p[i].y) > p[i].z) return;
  }
  if (glr::active()) {
    double a[3] = {p[0].x, p[0].y, p[0].z}, b[3] = {p[1].x, p[1].y, p[1].z};
    glr::note_line(a, b, gl_ops(line_ops()));
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
  const int xa = std::max(0, x0), xb = std::min(Wp, x1);
  const int ya = std::max(0, y0), yb = std::min(Hp, y1);
  if (xa >= xb || ya >= yb) return;
  if (coverage_only()) {
    const size_t start = size_t(xa / S), count = size_t((xb - 1) / S - xa / S + 1);
    for (int y = ya / S; y <= (yb - 1) / S; ++y)
      std::fill_n(&L.cover[size_t(y) * W + start], count, uint8_t(1));
    return;
  }
  for (int y = ya; y < yb; ++y)
    for (int x = xa; x < xb; ++x) put(L, x, y, o);
}

void on_point(int sel) {
  pending_3d = false;
  m68k::Cpu& c = native::g_machine->cpu;
  int16_t x = int16_t(c.d[0]), y = int16_t(c.d[1]), z = int16_t(c.d[2]);
  if (z <= 0 || std::abs(int(x)) >= z || std::abs(int(y)) >= z) return;
  int d7 = sel == INT32_MIN ? int16_t(rd16(0x5adb0)) : sel;
  // Resolve size kind at capture time (limit from -40(a6) is only valid now).
  int kind;  // 0: 1x1, 1: 2x1, 2: 2x2, 3: disc
  int disc_r = 0;
  if (d7 >= 0) {
    int limit = int16_t(rd16(c.a[6] - 40));
    if ((48 >> d7) >= limit) kind = 2;
    else if ((80 >> d7) >= limit) kind = 1;
    else kind = 0;
  } else if (d7 == -1) kind = 0;
  else if (d7 == -2) kind = 1;
  else if (d7 == -3) kind = 2;
  else if (d7 == -4) {
    kind = 3;
    disc_r = std::min(127, int(int16_t(c.d[6])));
    if (disc_r < 1) disc_r = 1;
  } else return;
  if (glr::active()) {
    double q[3] = {double(x), double(y), double(z)};
    glr::note_point(q, kind, disc_r, gl_ops(kind == 3 ? poly_ops() : line_ops()));
  }
  if (scene::enabled()) {
    scene::Prim pr;
    pr.kind = scene::PrimKind::Point;
    pr.n = 1;
    // sel encodes resolved kind: 0/1/2 size, or -4 for disc (radius in xyz[1][0])
    pr.sel = kind == 3 ? int16_t(-4) : int16_t(kind);
    pr.rgba = rgba_from_ops(line_ops());
    pr.xyz[0][0] = x;
    pr.xyz[0][1] = y;
    pr.xyz[0][2] = z;
    if (kind == 3) pr.xyz[1][0] = int16_t(disc_r);
    scene::note_prim(pr);
  }
  if (!first_prim()) return;
  V2 sp = project({double(x), double(y), double(z)});
  // low-res pixel centre of the projected position (the original rounds
  // to integer screen coordinates; +1 row as at $43902)
  double cx = sp.x + 0.5, cy = sp.y + 0.5 + 1.0;
  PlaneOps o = line_ops();
  Layer& L = *cur;
  if (kind == 0) fill_rect(L, cx, cy, 1, 1, o);
  else if (kind == 1) fill_rect(L, cx - 0.5, cy, 2, 1, o);
  else if (kind == 2) fill_rect(L, cx, cy, 2, 2, o);
  else {
    std::vector<V2> circ;
    for (int i = 0; i < 48; ++i) {
      double a = 2 * M_PI * i / 48;
      circ.push_back({cx + disc_r * std::cos(a), cy + disc_r * std::sin(a)});
    }
    fill_polygon(L, circ, poly_ops());
  }
}

void on_line() {
  V3 a{double(int16_t(rd16(0x61886))), double(int16_t(rd16(0x61888))), double(int16_t(rd16(0x6188a)))};
  V3 b{double(int16_t(rd16(0x6188c))), double(int16_t(rd16(0x6188e))), double(int16_t(rd16(0x61890)))};
  auto c = clip_near({a, b}, 1.0);
  if (c.size() != 2) return;
  if (glr::active()) {
    double ga[3] = {c[0].x, c[0].y, c[0].z}, gb[3] = {c[1].x, c[1].y, c[1].z};
    glr::note_line(ga, gb, gl_ops(line_ops()));
  }
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

void sky_quad_done(bool ok) {
  if (!scene_hooks) return;
  if (ok) on_horizon_poly_ready();
  else on_horizon_poly_fail();
}

void reset() {
  if (native::g_machine) native::g_machine->raster_audit_reset();
  layers.clear();
  reset_cockpit_atlas();
  reset_cockpit_instrument_state();
  gf_begun = false;
  cur = nullptr;
  in_scene = false;
  pending_3d = false;
}

static void do_begin();

// the layer starts at the top of game_frame ($2112c) so that the sky and
// ground drawn before $2fc14 are part of it; $2fc14 must not restart it
static void on_game_frame() {
  if (native::rd8(0x5aa8d) == 0) {
    // The pause/menu handler runs game_frame but does not finish a 3D
    // capture. Keep the last complete frame instead of replacing it with
    // an unfinished bitmap and a blank GL primitive list.
    if (in_scene && cur && !cur->done) {
      glr::cancel_capture();
      native::g_machine->raster_audit_scene(false);
      in_scene = false;
      cur = nullptr;
      for (auto& l : layers)
        if (l.done && (!cur || l.frame > cur->frame)) cur = &l;
    }
    gf_begun = false;
    return;
  }
  glr::scene_begin();
  do_begin();
  gf_begun = in_scene;
}

void scene_begin() {
  if (in_scene) {
    gf_begun = false;
    return;
  }
  do_begin();
}

static void do_begin() {
  amiga::Machine& m = *native::g_machine;
  amiga::Machine::RasterAuditScope audit(m, amiga::Machine::RasterSource::Presenter);
  // Open a SceneFrame at game_frame so horizon / sky drawn before $2fc14
  // are attributed to this frame (begin_frame is idempotent per frame_id).
  if (scene::enabled()) scene::begin_frame(m.frame_count());
  int S = m.hires_scale;
  if (S <= 0) return;
  if (glr::active()) m.raster_audit_scene(true);
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
  L->gl_cached = false;
  L->prims = 0;
  init_from_bitmap(*L);
  cur = L;
  in_scene = true;
}

void scene_end() {
  if (!in_scene || !cur) return;
  amiga::Machine& m = *native::g_machine;
  m.raster_audit_scene(false);
  amiga::Machine::RasterAuditScope audit(m, amiga::Machine::RasterSource::Presenter);
  in_scene = false;
  uint32_t pl[4];
  for (int i = 0; i < 4; ++i) pl[i] = plane_ptr(i);
  cur->snap.assign(size_t(W) * size_t(cur->rows), 0);
  for (int y = 0; y < cur->rows; ++y)
    for (int x = 0; x < W; ++x) cur->snap[size_t(y) * W + size_t(x)] = low_index(pl, x, y);
  cur->unexplained.assign(cur->snap.size(), 0);
  for (size_t i = 0; i < cur->snap.size(); ++i)
    cur->unexplained[i] = (cur->snap[i] != cur->s0[i]) && !cur->cover[i];
  if (std::getenv("FA18_GL_COVERAGE_DEBUG") && glr::active()) {
    uint64_t hash = UINT64_C(14695981039346656037);
    uint32_t count = 0;
    for (uint8_t value : cur->cover) {
      hash = (hash ^ value) * UINT64_C(1099511628211);
      count += value != 0;
    }
    std::fprintf(stderr, "gl: coverage frame=%llu rows=%d pixels=%u fnv=%016llx\n",
                 (unsigned long long)cur->frame, cur->rows, count,
                 (unsigned long long)hash);
  }
  // OpenGL renderer: replaces the re-rasterised layer; the coverage of the
  // original's 3D primitives above still decides where 2D drawing done
  // during the scene (target markers) shows through.
  if (glr::active()) {
    glr::finish_capture();
    if (!glr::deferred_scene_readback())
      cur->gl_cached = glr::render_layer(cur->px, W * cur->S, cur->rows * cur->S, cur->S);
  }
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
  scene_return_pcs.clear();
  install_cockpit_instrument_hooks(m);
  // The native $2fc14 wrapper invokes scene_begin/scene_end directly, but
  // --native off runs the original bytes and skips those C++ calls. Observe
  // its entry and the return address on the 68000 stack, then hook that
  // return instruction. Reusing the return hook avoids allocations per frame.
  m.add_pc_hook(0x2fc14, [&m] {
    if (!in_scene) glr::scene_begin();
    scene_begin();
    const uint32_t ret = rd32(native::cpu().a[7]) & 0x00ffffff;
    if (ret < 0x20000 || ret >= 0x80000 || (ret & 1)) return;
    if (std::find(scene_return_pcs.begin(), scene_return_pcs.end(), ret) != scene_return_pcs.end())
      return;
    scene_return_pcs.push_back(ret);
    m.add_pc_hook(ret, [] { scene_end(); });
    if (std::getenv("FA18_GL_DEBUG"))
      std::fprintf(stderr, "gl: hooked 68000 scene return at $%06x\n", ret);
  });
  // A native object wrapper calls begin_object after decoding the model
  // pointer. Recreate exactly that state at the original entry labels so
  // polygons and lines also get a depth/object ID in interpreter mode.
  m.add_pc_hook(0x31e4a, [] {
    const m68k::Cpu& c = native::cpu();
    const uint32_t mesh = rd32(c.a[6] - 44) + uint32_t(int16_t(c.d[0] & 4095));
    glr::begin_object(mesh, c.a[2], false);
  });
  m.add_pc_hook(0x32dda, [] {
    const m68k::Cpu& c = native::cpu();
    if (uint16_t(c.d[0]) != 0xffff)
      glr::begin_object(rd32(c.a[2]), c.a[2] + 4, true);
  });
  uint32_t dummy = 0;
  (void)dummy;
  // $38c0a falls into $38c0c — hook only $38c0c or every poly is captured twice.
  m.add_pc_hook(0x2112c, [] { on_game_frame(); });
  m.add_pc_hook(0x38c0c, [] { on_polygon(); });
  m.add_pc_hook(0x43a56, [] { on_line(); });
  m.add_pc_hook(0x4397c, [] { on_line_noclip(); });
  m.add_pc_hook(0x44b50, [] { on_fill2d(); });
  m.add_pc_hook(0x44b00, [] { on_fill_horizon(); });
  m.add_pc_hook(0x44f46, [] { on_fill_44f46(); });
  // Complex path: capture screen-space sea poly at $42f6a RTS.
  // Runtime = image offset + LOAD_BASE 0x20000 (objdump shows image offsets).
  m.add_pc_hook(0x432aa, [] { on_horizon_poly_ready(); });
  m.add_pc_hook(0x432b6, [] { on_horizon_poly_fail(); });
  // Simple path entry: plane clears then jsr $42f6a.
  m.add_pc_hook(0x44994, [] { on_simple_horizon(); });
  // $2506e: plane fill used by horizon (d0=8 clears plane 1 inside poly).
  m.add_pc_hook(0x4506e, [] {
    if (!std::getenv("FA18_HORIZON_DEBUG")) return;
    m68k::Cpu& c = native::g_machine->cpu;
    std::fprintf(stderr, "[horizon] 2506e d0=%d d3=%d d4=%d ab93=%d\n",
                 int16_t(c.d[0]), int16_t(c.d[3]), int16_t(c.d[4]), int(rd8(0x5ab93)));
  });
  // $3f284 terrain/ground after horizon when ab93==0
  m.add_pc_hook(0x3f284, [] {
    if (!std::getenv("FA18_HORIZON_DEBUG")) return;
    static int n = 0;
    if (n < 5) std::fprintf(stderr, "[horizon] 3f284 terrain enter\n");
    ++n;
  });
  // $44dfe entry: $60684 already holds count-prefixed screen-space poly
  // (recomp may not hit the exact RTS at $432aa).
  // $44dfe is also used by general 2D fills ($44b50).  Only capture when
  // called from the horizon path ($44994 → bsr $44dfe → return $44b00).
  m.add_pc_hook(0x44dfe, [] {
    uint32_t ret = native::rd32(native::g_machine->cpu.a[7]);
    // Only the horizon path ($44994 → $44afc bsr → return $44b00).
    if (ret != 0x44b00) return;
    if (std::getenv("FA18_HORIZON_DEBUG")) {
      int n = int16_t(rd16(0x60684));
      std::fprintf(stderr, "[horizon] SEA n=%d ab93=%d\n", n, int(rd8(0x5ab93)));
    }
    if (scene::enabled()) capture_horizon_fill();
  });
  // Count $42f6a outcomes via $5ab96 (set 1 on success path before RTS).
  m.add_pc_hook(0x42f6a, [] {
    if (!std::getenv("FA18_HORIZON_DEBUG")) return;
    static int n = 0;
    if (n < 5)
      std::fprintf(stderr, "[horizon] 42f6a enter camY=%d M0=%d\n",
                   int32_t(rd32(0x5ad5e)), int16_t(rd16(0x5aed0)));
    ++n;
  });
  m.add_pc_hook(0x42f62, [] {
    if (!std::getenv("FA18_HORIZON_DEBUG")) return;
    std::fprintf(stderr, "[horizon] 42f62 enter (alt matrix)\n");
  });
  // dots/discs and other direct fill callers: reset the pairing flag
  m.add_pc_hook(0x438a0, [] { on_point(INT32_MIN); });
  m.add_pc_hook(0x438a8, [] { on_point(-4); });
  m.add_pc_hook(0x438ac, [] { on_point(-3); });
  m.add_pc_hook(0x438b0, [] { on_point(-2); });
  m.add_pc_hook(0x438b4, [] { on_point(-1); });
  m.add_pc_hook(0x4389c, [] { on_point(-5); });
}

namespace {
void blend_native_fuel_digits(std::vector<uint32_t>& out, int side, int scale,
                              const FuelDigits& fuel);
void blend_native_fuel_bar(std::vector<uint32_t>& out, int side, int scale,
                           const FuelBar& bar);
void blend_native_readout_digits(std::vector<uint32_t>& out, int side, int scale,
                                 const FlightReadouts& readouts, const uint32_t* palette);
void blend_native_radar_symbols(std::vector<uint32_t>& out, int side, int scale,
                                const RadarSymbols& radar);
}

bool compose(amiga::Machine& m, std::vector<uint32_t>& out) {
  amiga::Machine::RasterAuditScope audit(m, amiga::Machine::RasterSource::Presenter);
  const int S = m.hires_scale;
  if (S <= 0) return false;
  // A window/mode switch can leave a deferred GL capture that was never
  // read back. Materialise the current scene on demand when returning to
  // the regular (non-widescreen, 60 Hz) composer; older buffers fall back
  // to original pixels until they receive a fresh GL capture.
  if (glr::active() && cur && cur->done && !cur->gl_cached)
    cur->gl_cached = glr::render_layer(cur->px, W * S, cur->rows * S, S);
  const int Wp = W * S, Hp = 200 * S;
  out.resize(size_t(Wp) * size_t(Hp));
  const uint32_t* rgb = m.frame_rgb();
  const uint8_t* idx = m.frame_idx();
  const uint32_t* key = m.frame_row_key();
  const uint32_t* pal = m.frame_row_pal();
  if (!idx || !key) return false;
  const char* native_cockpit = std::getenv("FA18_COCKPIT_NATIVE");
  const bool cockpit_enabled = !native_cockpit || std::strcmp(native_cockpit, "0") != 0;
  const FuelDigits fuel = prepare_fuel_digits(m, cockpit_atlas(m), key, pal, cockpit_enabled);
  const FuelBar bar = prepare_fuel_bar(fuel, pal, S);
  const FlightReadouts readouts = prepare_flight_readouts(m, key, pal, cockpit_enabled);
  const RadarSymbols radar = prepare_radar_symbols(m, rgb, key, pal, cockpit_enabled);
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
    if (L && glr::active() && !L->gl_cached) L = nullptr;
    for (int yy = 0; yy < S; ++yy) {
      uint32_t* o = &out[size_t(y * S + yy) * size_t(Wp)];
      for (int x = 0; x < W; ++x) {
        if (radar.source_at(x, y, rgb[size_t(y) * W + x])) {
          for (int xx = 0; xx < S; ++xx) o[x * S + xx] = radar.backing(y);
          continue;
        }
        if (bar.contains(x, y)) {
          const uint32_t colour = bar.backing(y);
          for (int xx = 0; xx < S; ++xx) o[x * S + xx] = colour;
          continue;
        }
        if (fuel.contains(x, y)) {
          const uint32_t colour = fuel.backing(x, y);
          for (int xx = 0; xx < S; ++xx) o[x * S + xx] = colour;
          continue;
        }
        if (readouts.contains(x, y)) {
          const uint32_t colour = readouts.backing(x, y, pal);
          for (int xx = 0; xx < S; ++xx) o[x * S + xx] = colour;
          continue;
        }
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
  blend_native_fuel_bar(out, 0, S, bar);
  blend_native_fuel_digits(out, 0, S, fuel);
  blend_native_readout_digits(out, 0, S, readouts, pal);
  blend_native_radar_symbols(out, 0, S, radar);
  return true;
}

// Same antialiased 960x720 crosshair geometry as the visible GPU shader.
// Its original 5x5 bitmap pixels are cleared from the 2D overlay first.
float hud_segment_distance(float px, float py, float ax, float ay, float bx, float by) {
  const float dx = bx - ax, dy = by - ay;
  const float t = std::clamp(((px - ax) * dx + (py - ay) * dy) /
                               (dx * dx + dy * dy), 0.f, 1.f);
  return std::hypot(px - ax - t * dx, py - ay - t * dy);
}

void blend_hud_pixel(uint32_t& dst, uint32_t rgb, float coverage) {
  const uint32_t a = uint32_t(std::lround(255.f * std::clamp(coverage, 0.f, 1.f)));
  if (!a) return;
  uint32_t colour = 0xff000000u;
  for (int shift = 0; shift < 24; shift += 8) {
    const uint32_t v = ((((rgb >> shift) & 255) * a) +
                        (((dst >> shift) & 255) * (255 - a)) + 127) / 255;
    colour |= v << shift;
  }
  dst = colour;
}

void blend_native_reticle(std::vector<uint32_t>& out, int side, int scale,
                          const NativeHudState& hud) {
  if (!hud.reticle) return;
  const int stride = (W + 2 * side) * scale;
  for (int y = 70 * scale; y < 77 * scale; ++y)
    for (int x = 155 * scale; x < 162 * scale; ++x) {
      const float px = (float(x) + .5f) * 3.f / scale;
      const float py = (float(y) + .5f) * 3.6f / scale;
      const float d = std::min(hud_segment_distance(px, py, 478.5f, 257.4f, 478.5f, 271.8f),
                               hud_segment_distance(px, py, 472.5f, 264.6f, 484.5f, 264.6f));
      uint32_t& dst = out[size_t(y) * stride + size_t((side * scale) + x)];
      blend_hud_pixel(dst, hud.reticle_rgb, 1.8f - d);
    }
}

// The 132-pixel top rail and the two 8-pixel chamfers of the HUD window
// were single coarse Amiga pixels. Keep their two live copper pen colours,
// but rasterise their lines directly in 960x720 corrected coordinates.
// The rails are drawn separately after restoring their scene/cockpit backing.
void blend_native_frame_crown(std::vector<uint32_t>& out, int side, int scale,
                              const NativeHudState& hud) {
  if (!hud.frame_crown) return;
  const int stride = (W + 2 * side) * scale;
  for (int y = 35 * scale; y < 48 * scale; ++y)
    for (int x = 83 * scale; x < 236 * scale; ++x) {
      const float px = (float(x) + .5f) * 3.f / scale;
      const float py = (float(y) + .5f) * 3.6f / scale;
      const float top = hud_segment_distance(px, py, 280.5f, 135.f, 673.5f, 135.f);
      const float left = hud_segment_distance(px, py, 277.5f, 138.6f, 256.5f, 163.8f);
      const float right = hud_segment_distance(px, py, 676.5f, 135.f, 700.5f, 163.8f);
      uint32_t& dst = out[size_t(y) * stride + size_t(side * scale + x)];
      blend_hud_pixel(dst, hud.frame_top_rgb, 1.6f - top);
      blend_hud_pixel(dst, hud.frame_corner_rgb, 1.6f - std::min(left, right));
    }
}

// Vertical edges of the same HUD window. The top uses the GL scene behind
// the source bitmap; the last seven rows cross the foreground cockpit art.
// This native stroke is narrow and antialiased rather than a 3x-wide block.
void blend_native_frame_rails(std::vector<uint32_t>& out, int side, int scale,
                              const NativeHudState& hud) {
  if (!hud.frame_rails) return;
  const int stride = (W + 2 * side) * scale;
  for (int y = 45 * scale; y < 146 * scale; ++y)
    for (int source_x : {85, 233})
      for (int x = (source_x - 2) * scale; x < (source_x + 3) * scale; ++x) {
        const float px = (float(x) + .5f) * 3.f / scale;
        const float py = (float(y) + .5f) * 3.6f / scale;
        const float d = hud_segment_distance(px, py, (source_x + .5f) * 3.f, 167.4f,
                                              (source_x + .5f) * 3.f, 520.2f);
        uint32_t& dst = out[size_t(y) * stride + size_t(side * scale + x)];
        blend_hud_pixel(dst, hud.frame_rail_rgb, 1.6f - d);
      }
}

// HUD text is captured from the original screen-space font writer when
// possible; the complete bitplane-stencil recogniser remains a fallback
// until each of the two screen buffers has received its first redraw.
namespace {

float hud_digit_coverage(float px, float py, const HudDigitSlot& slot, uint8_t mask) {
  const float left = (slot.x + .5f) * 3.f, right = (slot.x + 2.5f) * 3.f;
  const float midx = (slot.x + 1.5f) * 3.f;
  const float top = (slot.y + .5f) * 3.6f, middle = (slot.y + 2.5f) * 3.6f;
  const float bottom = (slot.y + 4.5f) * 3.6f;
  if (mask == 128)
    return 1.9f - hud_segment_distance(px, py, midx, top, midx, bottom);
  float dist = 1e9f;
  if (mask & 1)  dist = std::min(dist, hud_segment_distance(px, py, left, top, right, top));
  if (mask & 2)  dist = std::min(dist, hud_segment_distance(px, py, right, top, right, middle));
  if (mask & 4)  dist = std::min(dist, hud_segment_distance(px, py, right, middle, right, bottom));
  if (mask & 8)  dist = std::min(dist, hud_segment_distance(px, py, left, bottom, right, bottom));
  if (mask & 16) dist = std::min(dist, hud_segment_distance(px, py, left, middle, left, bottom));
  if (mask & 32) dist = std::min(dist, hud_segment_distance(px, py, left, top, left, middle));
  if (mask & 64) dist = std::min(dist, hud_segment_distance(px, py, left, middle, right, middle));
  return 1.9f - dist;
}

void blend_native_hud_digits(std::vector<uint32_t>& out, int side, int scale,
                             const NativeHudState& hud) {
  const int stride = (W + side * 2) * scale;
  for (size_t i = 0; i < kHudDigitSlots.size(); ++i) {
    const uint8_t mask = hud.digit_strokes[i];
    if (!mask) continue;
    const auto& slot = kHudDigitSlots[i];
    for (int y = (slot.y - 1) * scale; y < (slot.y + 6) * scale; ++y)
      for (int x = (slot.x - 1) * scale; x < (slot.x + 4) * scale; ++x) {
        const float px = (x + .5f) * 3.f / scale;
        const float py = (y + .5f) * 3.6f / scale;
        uint32_t& dst = out[size_t(y) * stride + size_t((side * scale) + x)];
        blend_hud_pixel(dst, hud.digit_colours[slot.group],
                        hud_digit_coverage(px, py, slot, mask));
      }
  }
}

// The five fuel-LCD values are already sourced from the original player
// record and latched by the double-buffered font writer. Unlike the tiny
// 3x5 bitplane glyphs, these native strokes are rasterised at output size.
// Confine replacement to the LCD's 19x5 source-pixel rectangle so the frame
// artwork surrounding it remains untouched.
void blend_native_fuel_digits(std::vector<uint32_t>& out, int side, int scale,
                              const FuelDigits& fuel) {
  if (!fuel.vector_enabled) return;
  const int stride = (W + side * 2) * scale;
  for (int y = FuelDigits::Y * scale; y < (FuelDigits::Y + FuelDigits::H) * scale; ++y)
    for (int x = FuelDigits::X * scale; x < (FuelDigits::X + FuelDigits::W) * scale; ++x) {
      const int digit = (x / scale - FuelDigits::X) / 4;
      if (digit < 0 || digit >= 5 || (x / scale - FuelDigits::X) % 4 == 3) continue;
      const HudDigitSlot slot{FuelDigits::X + digit * 4, FuelDigits::Y, 0, 0};
      const float px = (x + .5f) * 3.f / scale;
      const float py = (y + .5f) * 3.6f / scale;
      uint32_t& dst = out[size_t(y) * stride + size_t(side * scale + x)];
      blend_hud_pixel(dst, fuel.ink_rgb,
                      hud_digit_coverage(px, py, slot, fuel.stroke_masks[size_t(digit)]));
    }
}

// The original bar is a 4x23 rectangle, with 0..23 filled rows. Replace
// enlarged low-resolution pixels with a scanline-palette-correct native
// rectangle, giving the narrow sides and the changing upper edge subpixel
// coverage. Its surrounding green tick marks are separate static artwork.
void blend_native_fuel_bar(std::vector<uint32_t>& out, int side, int scale,
                           const FuelBar& bar) {
  if (!bar.active || !bar.filled_rows) return;
  const int stride = (W + side * 2) * scale;
  const float top = float(FuelBar::Y + FuelBar::H - bar.filled_rows);
  for (int y = FuelBar::Y * scale; y < (FuelBar::Y + FuelBar::H) * scale; ++y)
    for (int x = FuelBar::X * scale; x < (FuelBar::X + FuelBar::W) * scale; ++x) {
      const float px = (x + .5f) / scale;
      const float py = (y + .5f) / scale;
      const float coverage =
        std::clamp((px - FuelBar::X) * scale, 0.f, 1.f) *
        std::clamp((FuelBar::X + FuelBar::W - px) * scale, 0.f, 1.f) *
        std::clamp((py - top) * scale, 0.f, 1.f) *
        std::clamp((FuelBar::Y + FuelBar::H - py) * scale, 0.f, 1.f);
      uint32_t& dst = out[size_t(y) * stride + size_t(side * scale + x)];
      blend_hud_pixel(dst, bar.ink_rgb, coverage);
    }
}

// Native radar guides, centre marker and contact use the captured original
// blitter/CPU writer coordinates. The two screen banks can have different
// one-pixel vertical shifts during a transition. Copper supplies all inks.
void blend_native_radar_symbols(std::vector<uint32_t>& out, int side, int scale,
                                const RadarSymbols& radar) {
  if (!radar.active) return;
  const int stride = (W + side * 2) * scale;
  for (int y = RadarSymbols::Y * scale;
       y < (RadarSymbols::Y + RadarSymbols::H) * scale; ++y)
    for (int x = RadarSymbols::X * scale;
         x < (RadarSymbols::X + RadarSymbols::W) * scale; ++x) {
      const float px = (x + .5f) / scale, py = (y + .5f) / scale;
      uint32_t& dst = out[size_t(y) * stride + size_t(side * scale + x)];
      const float gy = float(radar.guide_y) + .5f;
      const float mx = float(radar.marker_x) + .5f;
      const float my = float(radar.marker_y) + .5f;
      const float guide = std::min(
        hud_segment_distance(px, py, 139.5f, gy, 154.5f, gy + 15.f),
        hud_segment_distance(px, py, 177.5f, gy, 162.5f, gy + 15.f));
      blend_hud_pixel(dst, radar.guide_rgb, 1.f - guide * scale);
      const float marker = std::min(
        hud_segment_distance(px, py, mx, my, mx - 1.f, my + 1.f),
        hud_segment_distance(px, py, mx - 1.f, my + 1.f, mx + 1.f, my + 1.f));
      blend_hud_pixel(dst, radar.marker_rgb, 1.f - marker * scale);
      const float dot = std::hypot(px - radar.dot_x - .5f, py - radar.dot_y - .5f);
      blend_hud_pixel(dst, radar.dot_rgb, 1.f - dot * scale);
    }
}

// Every completed source-font readout has a native seven-stroke replacement.
// Render only its 3x5 source rectangle, not the surrounding instrument bezel;
// the pen is resolved against the current copper palette for every row.
void blend_native_readout_digits(std::vector<uint32_t>& out, int side, int scale,
                                 const FlightReadouts& readouts, const uint32_t* palette) {
  const int stride = (W + side * 2) * scale;
  for (int i = 0; i < readouts.vector_count; ++i) {
    const auto& v = readouts.vectors[std::size_t(i)];
    const HudDigitSlot slot{int(v.x), int(v.y), 0, 0};
    for (int y = int(v.y) * scale; y < (int(v.y) + 5) * scale; ++y)
      for (int x = int(v.x) * scale; x < (int(v.x) + 3) * scale; ++x) {
        const float px = (x + .5f) * 3.f / scale;
        const float py = (y + .5f) * 3.6f / scale;
        const uint32_t ink = palette[size_t(y / scale) * 32 + v.ink_pen] & 0xffffffu;
        uint32_t& dst = out[size_t(y) * stride + size_t(side * scale + x)];
        blend_hud_pixel(dst, ink, hud_digit_coverage(px, py, slot, v.strokes));
      }
  }
}

float hud_letter_coverage(float px, float py, const HudLetterSlot& slot) {
  const float l = (slot.x + .5f) * 3.f, m = (slot.x + 1.5f) * 3.f;
  const float r = (slot.x + 2.5f) * 3.f;
  const float t = (slot.y + .5f) * 3.6f, c = (slot.y + 2.5f) * 3.6f;
  const float b = (slot.y + 4.5f) * 3.6f;
  float d = 1e9f;
  const auto line = [&](float x0, float y0, float x1, float y1) {
    d = std::min(d, hud_segment_distance(px, py, x0, y0, x1, y1));
  };
  if (slot.letter == 'K') {
    line(l, t, l, b); line(r, t, l, c); line(l, c, r, b);
  } else if (slot.letter == 'T') {
    line(l, t, r, t); line(m, t, m, b);
  } else if (slot.letter == 'F') {
    line(l, t, l, b); line(l, t, r, t); line(l, c, r, c);
  } else if (slot.letter == 'G') {
    line(r, t, m, t); line(m, t, l, t + 3.6f);
    line(l, t + 3.6f, l, b - 3.6f);
    line(l, b - 3.6f, m, b); line(m, b, r, b);
    line(r, c, r, b - 3.6f);
  }
  return 1.9f - d;
}

void blend_native_hud_letters(std::vector<uint32_t>& out, int side, int scale,
                              const NativeHudState& hud) {
  const int stride = (W + side * 2) * scale;
  for (size_t i = 0; i < kHudLetterSlots.size(); ++i) {
    if (!hud.letter_visible[i]) continue;
    const auto& slot = kHudLetterSlots[i];
    for (int y = (slot.y - 1) * scale; y < (slot.y + 6) * scale; ++y)
      for (int x = (slot.x - 1) * scale; x < (slot.x + 4) * scale; ++x) {
        const float px = (x + .5f) * 3.f / scale;
        const float py = (y + .5f) * 3.6f / scale;
        uint32_t& dst = out[size_t(y) * stride + size_t(side * scale + x)];
        blend_hud_pixel(dst, hud.letter_colours[i], hud_letter_coverage(px, py, slot));
      }
  }
}

float hud_heading_coverage(float px, float py, int first_x) {
  const float upper = 60.5f * 3.6f, lower = 61.5f * 3.6f;
  float d = hud_segment_distance(px, py, 159.5f * 3.f, 63.5f * 3.6f,
                                 159.5f * 3.f, 65.5f * 3.6f);
  for (int i = 0; i < 5; ++i) {
    const float x = (first_x + i * 10 + .5f) * 3.f;
    if (i % 2 == 0)
      d = std::min(d, hud_segment_distance(px, py, x, upper, x, lower));
    else
      d = std::min(d, std::hypot(px - x, py - lower));
  }
  return 1.7f - d;
}

void blend_native_heading_marks(std::vector<uint32_t>& out, int side, int scale,
                                const NativeHudState& hud) {
  if (!hud.heading_marks) return;
  const int stride = (W + side * 2) * scale;
  for (int y = 59 * scale; y < 67 * scale; ++y)
    for (int x = (hud.heading_first_x - 1) * scale;
         x < (hud.heading_first_x + 42) * scale; ++x) {
      const float px = (x + .5f) * 3.f / scale;
      const float py = (y + .5f) * 3.6f / scale;
      uint32_t& dst = out[size_t(y) * stride + size_t(side * scale + x)];
      blend_hud_pixel(dst, hud.heading_rgb,
                      hud_heading_coverage(px, py, hud.heading_first_x));
    }
}

int hud_original_digit_cell(int x, int y, const NativeHudState& hud) {
  const int slot = hud_font::source_text_cells().digit_at(x, y);
  return slot >= 0 && hud.digit_strokes[size_t(slot)] ? slot : -1;
}

int hud_original_letter_cell(int x, int y, const NativeHudState& hud) {
  const int slot = hud_font::source_text_cells().label_at(x, y);
  return slot >= 0 && hud.letter_visible[size_t(slot)] ? slot : -1;
}

bool hud_original_heading_pixel(int x, int y, const NativeHudState& hud) {
  if (!hud.heading_marks) return false;
  if (x == 159 && y >= 63 && y <= 65) return true;
  if (y != 60 && y != 61) return false;
  const int delta = x - hud.heading_first_x;
  if (delta < 0 || delta > 40 || delta % 10 != 0) return false;
  return y == 61 || (delta / 10) % 2 == 0;
}

// The font and compass geometry comes from complete screen-bank writer
// captures. In that case erase only the exact source 3x5/point strokes and
// reconstruct their background from the current scanline copper palette.
// The source-colour fallback remains for unobserved/partial redraws.
std::optional<uint32_t> hud_original_text_background(
    int x, int y, const NativeHudState& hud, const uint32_t* rgb,
    const uint32_t* palette, bool native_erase) {
  if (hud_original_heading_pixel(x, y, hud)) {
    const bool writer = native_erase && hud.heading_from_writer;
    if (hud_erase::erase_pixel(hud.heading_from_writer, native_erase,
                               writer ? 0 : rgb[y * W + x], hud.heading_rgb)) {
      return writer ? palette[size_t(y) * 32 + 5] :
                      rgb[y * W + (x == 159 ? 165 : x + 2)];
    }
  }
  if (const int letter = hud_original_letter_cell(x, y, hud); letter >= 0) {
    const auto& slot = kHudLetterSlots[size_t(letter)];
    const bool writer = native_erase && hud.letter_from_writer[size_t(letter)];
    const bool ink = writer ? hud_font::writer_letter_pixel(
                                  slot.letter, x - slot.x, y - slot.y) :
                              (rgb[y * W + x] & 0xffffffu) ==
                                  hud.letter_colours[size_t(letter)];
    if (ink) {
      const int pen = hud_backdrop_pen(slot.y, slot.background_x);
      return writer && pen >= 0 ? palette[size_t(y) * 32 + size_t(pen)] :
                                  rgb[y * W + slot.background_x];
    }
  }
  if (const int digit = hud_original_digit_cell(x, y, hud); digit >= 0) {
    const auto& slot = kHudDigitSlots[size_t(digit)];
    const uint8_t value = hud.digit_writer_value[size_t(digit)];
    const bool writer = native_erase && value != 0;
    const bool ink = writer ? hud_font::writer_digit_pixel(
                                  value, x - slot.x, y - slot.y) :
                              (rgb[y * W + x] & 0xffffffu) ==
                                  hud.digit_colours[slot.group];
    if (ink) {
      const int pen = hud_backdrop_pen(slot.y, slot.background_x);
      return writer && pen >= 0 ? palette[size_t(y) * 32 + size_t(pen)] :
                                  rgb[y * W + slot.background_x];
    }
  }
  return std::nullopt;
}
}  // namespace

// Exact locations of the original writers' 320x200 strokes. The fallback
// only compares ink in unobserved/partial banks; a complete writer capture
// erases these footprints without asking the old bitmap what colour they are.
bool frame_crown_source_pixel(int x, int y, const NativeHudState& hud,
                              uint32_t colour, bool native_erase) {
  return hud.frame_crown && hud_erase::frame_crown(x, y) &&
         hud_erase::erase_pixel(hud.frame_from_writer, native_erase, colour,
                               y == 37 && x < 225 ? hud.frame_top_rgb :
                                                     hud.frame_corner_rgb);
}

uint32_t frame_crown_background(const uint32_t* rgb, int x, int y) {
  return rgb[(y == 37 ? 36 : y) * W +
             (y == 37 ? x : (x < 159 ? x - 2 : x + 2))];
}

bool frame_rail_source_pixel(int x, int y, const NativeHudState& hud,
                             uint32_t colour, bool native_erase) {
  return hud.frame_rails && hud_erase::frame_rail(x, y) &&
         hud_erase::erase_pixel(hud.frame_from_writer, native_erase, colour,
                               hud.frame_rail_rgb);
}

// The forward cockpit lip begins at y=138. Its artwork obscures the world,
// so the two neighbouring original pixels are the best local continuation
// of that surface. Above it the GL scene itself supplies the background.
uint32_t frame_rail_cockpit_background(const uint32_t* rgb, int x, int y) {
  const uint32_t a = rgb[y * W + x - 1], b = rgb[y * W + x + 1];
  uint32_t c = 0xff000000u;
  for (int shift = 0; shift < 24; shift += 8)
    c |= ((((a >> shift) & 255u) + ((b >> shift) & 255u) + 1) / 2) << shift;
  return c;
}

// An unfinished back-buffer draw must not suppress presentation of the
// preceding complete scene.  The original Amiga display also keeps showing
// that front buffer while the next frame is still being drawn.
static const Layer* completed_scene_layer() {
  if (cur && cur->done) return cur;
  const Layer* latest = nullptr;
  for (const Layer& layer : layers)
    if (layer.done && (!latest || layer.frame > latest->frame)) latest = &layer;
  return latest;
}

// Diagnostics for reverse-engineering the remaining moving lower-panel art.
// Compare live ADF indices *and* RGB, and report only once per screen frame
// per presenter. This does not alter emulated RAM or the rendered pixels.
static void log_cockpit_dynamics(const char* presenter, const amiga::Machine& m,
                                const CockpitAtlas& atlas, const uint8_t* indices,
                                const uint32_t* rgb, const uint32_t* keys) {
  if (!std::getenv("FA18_COCKPIT_DYNAMIC_AUDIT") || !indices || !rgb || !keys ||
      m.frame_count() % 200 != 0) return;
  static uint64_t last_cpu = UINT64_MAX, last_gpu = UINT64_MAX;
  uint64_t& last = presenter[0] == 'C' ? last_cpu : last_gpu;
  if (last == m.frame_count()) return;
  last = m.frame_count();
  for (const auto& zone : cockpit_dynamic_audit::kRegions) {
    const auto measured = cockpit_dynamic_audit::inspect(atlas, indices, rgb, zone);
    std::fprintf(stderr,
                 "cockpit dynamic %s frame=%llu bank=%06x zone=%s "
                 "sample=%d index=%d rgb=%d changed=%d bounds=%d,%d..%d,%d\n",
                 presenter, (unsigned long long)m.frame_count(),
                 keys[183], zone.name, measured.samples,
                 measured.index_differences, measured.rgb_differences,
                 measured.full_differences, measured.min_x, measured.min_y,
                 measured.max_x, measured.max_y);
  }
}

bool compose_view(amiga::Machine& m, std::vector<uint32_t>& out, int side,
                  float alpha, bool refresh_center) {
  amiga::Machine::RasterAuditScope audit(m, amiga::Machine::RasterSource::Presenter);
  const Layer* ready = completed_scene_layer();
  if (!glr::active() || !ready || m.hires_scale <= 0 || side < 0)
    return false;
  const int S = m.hires_scale;
  const int wide = W + side * 2, stride = wide * S, left = side * S;
  // The GL projection keeps the original 320-column image in the centre.
  // Re-rendering from the two cached captures is independent of simulation.
  static std::vector<uint8_t> scene_pixels;
  // The original stops drawing its 3D layer at the top of the cockpit.
  // Widescreen has no cockpit art outside the central 320 columns, so
  // project the scene all the way to the bottom of those side extensions.
  if (!glr::render_layer(scene_pixels, stride, 200 * S, S, alpha)) return false;
  const uint32_t* rgb = m.frame_rgb();
  const uint8_t* idx = m.frame_idx();
  const uint32_t* key = m.frame_row_key();
  const uint32_t* pal = m.frame_row_pal();
  if (!rgb || !idx || !key || !pal) return false;
  const CockpitAtlas& cockpit = cockpit_atlas(m);
  log_cockpit_dynamics("CPU", m, cockpit, idx, rgb, key);
  // Diagnostic oracle: compare the asset-backed composition to the previous
  // bitplane-backed path without changing any emulated code or game state.
  const char* native_cockpit = std::getenv("FA18_COCKPIT_NATIVE");
  const bool cockpit_enabled = !native_cockpit || std::strcmp(native_cockpit, "0") != 0;
  const FuelDigits fuel = prepare_fuel_digits(m, cockpit, key, pal, cockpit_enabled);
  const FuelBar bar = prepare_fuel_bar(fuel, pal, S);
  const FlightReadouts readouts = prepare_flight_readouts(m, key, pal, cockpit_enabled);
  const RadarSymbols radar = prepare_radar_symbols(m, rgb, key, pal, cockpit_enabled);
  const uint32_t state = native::rd32(0x2a6d4);
  // Some flight transition handlers clear in_flight while they continue to
  // display freshly captured 3D frames. Testing only that flag makes the
  // widescreen sides blink black even though the centre still shows world.
  const bool recent_scene = m.frame_count() >= ready->frame &&
                            m.frame_count() - ready->frame <= 16;
  const bool flight = native::rd8(0x5aa8d) != 0 || recent_scene ||
                      state == 0x23b7e || state == 0x23ba8; // frozen Pause world
  const bool paused = pause_screen_active();
  const NativeHudState hud = native_hud_state(m);
  const bool pause_backdrop_copper =
      std::getenv("FA18_PAUSE_BACKDROP_REFERENCE") == nullptr;
  const bool native_erase = std::getenv("FA18_HUD_ERASE_BITMAP_REFERENCE") == nullptr;
  const bool native_text_erase =
      std::getenv("FA18_HUD_TEXT_ERASE_BITMAP_REFERENCE") == nullptr;
  const bool frame_writer_erase = native_erase && hud.frame_from_writer;
  const bool reticle_writer_erase = native_erase && hud.reticle_from_writer;
  const bool rail_asset_enabled = frame_writer_erase && cockpit_enabled &&
      std::getenv("FA18_HUD_RAIL_ART_BITMAP_REFERENCE") == nullptr;
  const bool front_asset_enabled = cockpit_enabled && hud.frame_from_writer &&
      hud.frame_rails && std::getenv("FA18_COCKPIT_FRONT_BITMAP_REFERENCE") == nullptr;
  const bool panel_asset_enabled = cockpit_enabled &&
      cockpit_panel::enabled(hud.frame_from_writer, hud.frame_rails, fuel.active) &&
      std::getenv("FA18_COCKPIT_PANEL_BITMAP_REFERENCE") == nullptr;
  const CapturedGlobe globe = panel_asset_enabled ? captured_globe(key) : CapturedGlobe{};
  int static_pixels = 0, dynamic_pixels = 0;
  int rail_asset_pixels = 0;
  int front_asset_pixels = 0;
  int panel_asset_pixels = 0;
  out.assign(size_t(stride) * size_t(200 * S), 0xFF000000u);
  for (int y = 0; y < 200; ++y) {
    const Layer* L = nullptr;
    int ly = -1;
    for (auto& l : layers) {
      if (!l.done || key[y] < l.base) continue;
      uint32_t d = key[y] - l.base;
      if (d % 40) continue;
      int row = int(d / 40);
      if (row < l.rows) { L = &l; ly = row; break; }
    }
    const auto row_map = gl_scene_rows::select(y, ready->rows,
        L ? ly : -1, flight, idx[y * W], idx[y * W + W - 1]);
    for (int yy = 0; yy < S; ++yy) {
      uint32_t* dst = &out[size_t(y * S + yy) * size_t(stride)];
      // The display may still point at a previous completed 3D layer
      // (Amiga double buffering) while `cur` holds the newest capture.
      // Use the current GL projection for its side bands whenever the
      // scanline belongs to a 3D layer; the original centre remains exact.
      if (row_map.side_scene && side > 0) {
        const uint8_t* scene_row =
            &scene_pixels[size_t(row_map.scene_y * S + yy) * size_t(stride)];
        // Beyond the old viewport use the last scene palette, not the
        // cockpit's scanline palette (which produces flashing side bands).
        const uint8_t left_bit = row_map.left_ehb ? 16 : 0;
        const uint8_t right_bit = row_map.right_ehb ? 16 : 0;
        for (int x = 0; x < left; ++x)
          dst[x] = pal[size_t(row_map.side_palette_y) * 32 +
                       ((scene_row[x] & 15) | left_bit)];
        for (int x = left + W * S; x < stride; ++x)
          dst[x] = pal[size_t(row_map.side_palette_y) * 32 +
                       ((scene_row[x] & 15) | right_bit)];
      }
      for (int x = 0; x < W; ++x) {
        if (const auto bg = hud_original_text_background(
                x, y, hud, rgb, pal, native_text_erase)) {
          for (int xx = 0; xx < S; ++xx) dst[left + x * S + xx] = *bg;
          continue;
        }
        // The native UI will draw the Pause heading at output resolution.
        // Erase only the old 320x200 title rectangle with its original
        // backdrop pen; its colour tracks the copper fades per scanline.
        if (hud.pause_title && y >= 42 && y <= 50 && x >= 124 && x <= 201) {
          const uint32_t bg = pause_backdrop_copper ?
              pal[size_t(y) * 32 + 5] : rgb[y * W + 120];
          for (int xx = 0; xx < S; ++xx) dst[left + x * S + xx] = bg;
          continue;
        }
        if (hud.reticle && hud_erase::reticle(x, y) &&
            hud_erase::erase_pixel(hud.reticle_from_writer, native_erase,
                                   reticle_writer_erase ? 0 : rgb[y * W + x],
                                   hud.reticle_rgb)) {
          const uint32_t bg = reticle_writer_erase ?
              pal[size_t(y) * 32 + 5] : rgb[y * W + 165];
          for (int xx = 0; xx < S; ++xx) dst[left + x * S + xx] = bg;
          continue;
        }
        if (frame_crown_source_pixel(x, y, hud,
                                     frame_writer_erase ? 0 : rgb[y * W + x],
                                     native_erase)) {
          const uint32_t bg = frame_writer_erase ?
              pal[size_t(y) * 32 + 5] : frame_crown_background(rgb, x, y);
          for (int xx = 0; xx < S; ++xx) dst[left + x * S + xx] = bg;
          continue;
        }
        if (frame_rail_source_pixel(x, y, hud,
                                    frame_writer_erase ? 0 : rgb[y * W + x],
                                    native_erase)) {
          const bool scene_behind = flight && y < 138 && y < ready->rows;
          if (scene_behind) {
            const int scene_y = L && ly >= 0 && ly < ready->rows ? ly : y;
            const uint8_t* src = &scene_pixels[size_t(scene_y * S + yy) * size_t(stride) +
                                               size_t(left + x * S)];
            for (int xx = 0; xx < S; ++xx)
              dst[left + x * S + xx] = pal[size_t(y) * 32 + (src[xx] & 15)];
          } else {
            const auto asset = rail_asset_enabled ?
                cockpit_rail::asset_background(cockpit, pal, x, y) : std::nullopt;
            const uint32_t bg = asset ? *asset : frame_rail_cockpit_background(rgb, x, y);
            if (asset && yy == 0) ++rail_asset_pixels;
            for (int xx = 0; xx < S; ++xx) dst[left + x * S + xx] = bg;
          }
          continue;
        }
        const uint8_t li = idx[y * W + x];
        if (radar.source_at(x, y, rgb[size_t(y) * W + x])) {
          const uint32_t colour = radar.backing(y);
          for (int xx = 0; xx < S; ++xx) dst[left + x * S + xx] = colour;
          continue;
        }
        if (bar.contains(x, y)) {
          const uint32_t colour = bar.backing(y);
          for (int xx = 0; xx < S; ++xx) dst[left + x * S + xx] = colour;
          continue;
        }
        if (fuel.contains(x, y)) {
          const uint32_t colour = fuel.backing(x, y);
          for (int xx = 0; xx < S; ++xx) dst[left + x * S + xx] = colour;
          continue;
        }
        if (readouts.contains(x, y)) {
          const uint32_t colour = readouts.backing(x, y, pal);
          for (int xx = 0; xx < S; ++xx) dst[left + x * S + xx] = colour;
          continue;
        }
        // The completed HUD-frame draw also establishes a visible cockpit
        // nose in this screen bank. Its nonblack ILBM cells are painted above
        // the 3D world, even when the old double-buffered bitmap disagrees.
        // Black ILBM cells are *not* a transparency mask; retain the original
        // compositor there, as well as for unobserved/partial frame redraws.
        if (front_asset_enabled) {
          if (const auto colour = cockpit_front::pixel(cockpit, pal, x, y)) {
            if (yy == 0) ++front_asset_pixels;
            for (int xx = 0; xx < S; ++xx) dst[left + x * S + xx] = *colour;
            continue;
          }
        }
        // Experimental native 68k-projected artificial horizon. Only the
        // original ADF fill stencil is recoloured; marks and bezel pixels
        // retain the preceding compositor. The copper supplies live colours.
        if (const auto pen = projected_globe_pen(globe, cockpit, x, y)) {
          const uint32_t colour = pal[size_t(y) * 32 + *pen];
          for (int xx = 0; xx < S; ++xx) dst[left + x * S + xx] = colour;
          continue;
        }
        // Fixed structural regions of inst5 are independently sourced from
        // the user's ADF. The panel's black pixels are opaque, unlike the
        // upper frnt5 silhouette; dynamic controls still use bitplanes.
        if (panel_asset_enabled) {
          if (const auto colour = cockpit_panel::pixel(cockpit, pal, x, y)) {
            if (yy == 0) ++panel_asset_pixels;
            for (int xx = 0; xx < S; ++xx) dst[left + x * S + xx] = *colour;
            continue;
          }
        }
        const size_t cp = size_t(y) * W + x;
        const bool index_matches = li == cockpit.index[cp];
        const bool colour_matches = (rgb[cp] & 0xFFFFFFu) == (cockpit.rgb[cp] & 0xFFFFFFu);
        if (cockpit_enabled && cockpit.valid[cp] && (index_matches || colour_matches)) {
          // The original bitplanes merely tell us which static pixels are
          // visible; the colour now comes from the decoded ADF artwork and
          // the *current scanline's* copper palette. A changed pixel belongs
          // to a dynamic gauge, label, or other foreground and stays original.
          const uint32_t colour = index_matches ? pal[size_t(y) * 32 + cockpit.index[cp]]
                                                : cockpit.rgb[cp];
          if (yy == 0) ++static_pixels;
          for (int xx = 0; xx < S; ++xx) dst[left + x * S + xx] = colour;
          continue;
        }
        if (yy == 0 && cockpit_enabled && cockpit.valid[cp]) ++dynamic_pixels;
        const size_t ci = size_t(ly < 0 ? 0 : ly) * W + size_t(x);
        // A scanline can temporarily point at a not-yet-completed Amiga
        // back buffer during a 3D redraw. It has no matching Layer; taking
        // that buffer as 2D foreground causes the whole centre row to flash
        // black while the GL widescreen sides remain stable. Genuine HUD
        // writes remain identifiable where a completed Layer does match.
        // In Pause the bitplanes contain the frozen game's real 2D panel
        // and gauges, not a half-drawn 3D back buffer. Keep them visible.
        const bool missing_3d_row = flight && !paused && y < ready->rows && !L;
        if (missing_3d_row ||
            (L && li % 16 == L->snap[ci] && !L->unexplained[ci])) {
          // The screen may point to the preceding Amiga 3D buffer. Its
          // contents are temporally different from the new GL side bands;
          // blending the sides against an old centre creates a flickering
          // join. Both sides and centre must use *one* GL scene. Unexplained
          // screen-space 2D drawing and the cockpit remain on top below.
          const int render_y = missing_3d_row ? y : ly;
          const uint8_t* src = &scene_pixels[size_t(render_y * S + yy) * size_t(stride) +
                                             size_t(left + x * S)];
          for (int xx = 0; xx < S; ++xx)
            dst[left + x * S + xx] = pal[size_t(y) * 32 +
                                             ((src[xx] & 15) | (missing_3d_row ? 0 : li & 16))];
        } else {
          const uint32_t colour = rgb[y * W + x];
          for (int xx = 0; xx < S; ++xx) dst[left + x * S + xx] = colour;
        }
      }
    }
  }
  if (std::getenv("FA18_COCKPIT_DEBUG") && static_pixels > 10000) {
    static bool reported = false;
    if (!reported) {
      reported = true;
      std::fprintf(stderr, "cockpit: CPU frame %llu native static=%d dynamic/fallback=%d\n",
                   (unsigned long long)m.frame_count(), static_pixels, dynamic_pixels);
    }
  }
  if (std::getenv("FA18_HUD_RAIL_ART_DEBUG") && m.frame_count() % 400 == 0) {
    static uint64_t reported = UINT64_MAX;
    if (reported != m.frame_count()) {
      reported = m.frame_count();
      std::fprintf(stderr, "HUD rail art CPU frame=%llu pixels=%d\n",
                   (unsigned long long)m.frame_count(), rail_asset_pixels);
    }
  }
  if (std::getenv("FA18_COCKPIT_FRONT_DEBUG") && m.frame_count() % 400 == 0) {
    static uint64_t reported = UINT64_MAX;
    if (reported != m.frame_count()) {
      reported = m.frame_count();
      std::fprintf(stderr, "cockpit front CPU frame=%llu pixels=%d\n",
                   (unsigned long long)m.frame_count(), front_asset_pixels);
    }
  }
  if (std::getenv("FA18_COCKPIT_PANEL_DEBUG") && m.frame_count() % 400 == 0) {
    static uint64_t reported = UINT64_MAX;
    if (reported != m.frame_count()) {
      reported = m.frame_count();
      std::fprintf(stderr, "cockpit panel CPU frame=%llu pixels=%d\n",
                   (unsigned long long)m.frame_count(), panel_asset_pixels);
    }
  }
  blend_native_reticle(out, side, S, hud);
  blend_native_frame_rails(out, side, S, hud);
  blend_native_frame_crown(out, side, S, hud);
  blend_native_hud_digits(out, side, S, hud);
  blend_native_fuel_bar(out, side, S, bar);
  blend_native_radar_symbols(out, side, S, radar);
  blend_native_fuel_digits(out, side, S, fuel);
  blend_native_readout_digits(out, side, S, readouts, pal);
  blend_native_hud_letters(out, side, S, hud);
  blend_native_heading_marks(out, side, S, hud);
  return true;
}

bool prepare_gpu_layers(amiga::Machine& m, GpuLayers& out) {
  const Layer* ready = completed_scene_layer();
  if (!glr::active() || !ready || m.hires_scale <= 0 ||
      ready->rows <= 0 || ready->rows >= 255) return false;
  const uint32_t* rgb = m.frame_rgb();
  const uint8_t* idx = m.frame_idx();
  const uint32_t* key = m.frame_row_key();
  const uint32_t* pal = m.frame_row_pal();
  if (!rgb || !idx || !key || !pal) return false;
  const CockpitAtlas& cockpit = cockpit_atlas(m);
  log_cockpit_dynamics("GPU", m, cockpit, idx, rgb, key);
  const char* native_cockpit = std::getenv("FA18_COCKPIT_NATIVE");
  const bool cockpit_enabled = !native_cockpit || std::strcmp(native_cockpit, "0") != 0;
  const FuelDigits fuel = prepare_fuel_digits(m, cockpit, key, pal, cockpit_enabled);
  const FuelBar bar = prepare_fuel_bar(fuel, pal, m.hires_scale);
  const FlightReadouts readouts = prepare_flight_readouts(m, key, pal, cockpit_enabled);
  const RadarSymbols radar = prepare_radar_symbols(m, rgb, key, pal, cockpit_enabled);
  out.fuel_strokes = fuel.stroke_masks;
  out.fuel_ink_rgb = fuel.ink_rgb;
  out.fuel_vector = fuel.vector_enabled;
  out.fuel_bar_ink_rgb = bar.ink_rgb;
  out.fuel_bar_rows = bar.filled_rows;
  out.fuel_bar_vector = bar.active;
  out.radar_vector = radar.active;
  out.radar_guide_rgb = radar.guide_rgb;
  out.radar_marker_rgb = radar.marker_rgb;
  out.radar_dot_rgb = radar.dot_rgb;
  out.radar_dot_x = radar.dot_x;
  out.radar_dot_y = radar.dot_y;
  out.radar_guide_y = radar.guide_y;
  out.radar_marker_x = radar.marker_x;
  out.radar_marker_y = radar.marker_y;

  // The original 3D view covers only the top of the cockpit, but widescreen
  // must project the world behind the entire uncockpitted side area.
  out.scene_rows = 200;
  const uint32_t state = native::rd32(0x2a6d4);
  const bool recent_scene = m.frame_count() >= ready->frame &&
                            m.frame_count() - ready->frame <= 16;
  const bool flight = native::rd8(0x5aa8d) != 0 || recent_scene ||
                      state == 0x23b7e || state == 0x23ba8;
  const bool paused = pause_screen_active();
  const NativeHudState hud = native_hud_state(m);
  const bool pause_backdrop_copper =
      std::getenv("FA18_PAUSE_BACKDROP_REFERENCE") == nullptr;
  const bool native_erase = std::getenv("FA18_HUD_ERASE_BITMAP_REFERENCE") == nullptr;
  const bool native_text_erase =
      std::getenv("FA18_HUD_TEXT_ERASE_BITMAP_REFERENCE") == nullptr;
  const bool frame_writer_erase = native_erase && hud.frame_from_writer;
  const bool reticle_writer_erase = native_erase && hud.reticle_from_writer;
  const bool rail_asset_enabled = frame_writer_erase && cockpit_enabled &&
      std::getenv("FA18_HUD_RAIL_ART_BITMAP_REFERENCE") == nullptr;
  const bool front_asset_enabled = cockpit_enabled && hud.frame_from_writer &&
      hud.frame_rails && std::getenv("FA18_COCKPIT_FRONT_BITMAP_REFERENCE") == nullptr;
  const bool panel_asset_enabled = cockpit_enabled &&
      cockpit_panel::enabled(hud.frame_from_writer, hud.frame_rails, fuel.active) &&
      std::getenv("FA18_COCKPIT_PANEL_BITMAP_REFERENCE") == nullptr;
  const CapturedGlobe globe = panel_asset_enabled ? captured_globe(key) : CapturedGlobe{};
  last_prepared_gpu_hud = hud;
  if (std::getenv("FA18_HUD_RETICLE_DEBUG") && m.frame_count() % 30 == 0)
    std::fprintf(stderr, "HUD state frame=%llu reticle=%d colour=%06x pause_title=%d\n",
                 (unsigned long long)m.frame_count(), int(hud.reticle), hud.reticle_rgb,
                 int(hud.pause_title));
  out.scene_palette_last = gl_scene_rows::last_palette_row(ready->rows);
  const bool global_palette_reference =
      std::getenv("FA18_GL_COPPER_GLOBAL_CLAMP_REFERENCE") != nullptr;
  out.overlay.resize(size_t(W) * 200 * 4);
  out.palette.resize(size_t(32) * 200 * 4);
  out.rows.resize(200 * 4);
  int static_pixels = 0, dynamic_pixels = 0;
  int rail_asset_pixels = 0;
  int front_asset_pixels = 0;
  int panel_asset_pixels = 0;
  for (int y = 0; y < 200; ++y) {
    const Layer* L = nullptr;
    int ly = -1;
    for (const auto& l : layers) {
      if (!l.done || key[y] < l.base) continue;
      const uint32_t d = key[y] - l.base;
      if (d % 40) continue;
      const int row = int(d / 40);
      if (row < l.rows) { L = &l; ly = row; break; }
    }
    const auto row_map = gl_scene_rows::select(y, ready->rows,
        L ? ly : -1, flight, idx[y * W], idx[y * W + W - 1]);
    const bool scene = row_map.has_scene_row;
    out.rows[size_t(y) * 4] = row_map.side_scene ? uint8_t(row_map.scene_y) : 255;
    out.rows[size_t(y) * 4 + 1] = row_map.side_scene ? 255 : 0;
    out.rows[size_t(y) * 4 + 2] = row_map.left_ehb ? 255 : 0;
    out.rows[size_t(y) * 4 + 3] = row_map.right_ehb ? 255 : 0;
    // The 320-column centre (ADF cockpit indices, native LCD/readouts,
    // original 2D pens and scene fallback) always uses live copper rows.
    // The visible shader clamps *only* widescreen side-band lookups.
    // Reference: restore the old *global* clamp to compare fades/colours.
    const int palette_y = global_palette_reference ?
        std::min(y, out.scene_palette_last) : y;
    for (int n = 0; n < 32; ++n) {
      const uint32_t c = pal[size_t(palette_y) * 32 + n];
      uint8_t* p = &out.palette[(size_t(y) * 32 + n) * 4];
      p[0] = uint8_t(c >> 16);
      p[1] = uint8_t(c >> 8);
      p[2] = uint8_t(c);
      p[3] = 255;
    }
    for (int x = 0; x < W; ++x) {
      const uint8_t li = idx[y * W + x];
      const size_t ci = size_t(scene ? ly : 0) * W + size_t(x);
      uint8_t* p = &out.overlay[(size_t(y) * W + size_t(x)) * 4];
      if (radar.source_at(x, y, rgb[size_t(y) * W + x])) {
        const uint32_t c = radar.backing(y);
        p[0] = uint8_t(c >> 16);
        p[1] = uint8_t(c >> 8);
        p[2] = uint8_t(c);
        p[3] = 255;
        continue;
      }
      if (bar.contains(x, y)) {
        const uint32_t c = bar.backing(y);
        p[0] = uint8_t(c >> 16);
        p[1] = uint8_t(c >> 8);
        p[2] = uint8_t(c);
        p[3] = 255;
        continue;
      }
      if (const auto bg = hud_original_text_background(
              x, y, hud, rgb, pal, native_text_erase)) {
        p[0] = uint8_t(*bg >> 16);
        p[1] = uint8_t(*bg >> 8);
        p[2] = uint8_t(*bg);
        p[3] = 255;
        continue;
      }
      if (hud.pause_title && y >= 42 && y <= 50 && x >= 124 && x <= 201) {
        const uint32_t bg = pause_backdrop_copper ?
            pal[size_t(y) * 32 + 5] : rgb[y * W + 120];
        p[0] = uint8_t(bg >> 16);
        p[1] = uint8_t(bg >> 8);
        p[2] = uint8_t(bg);
        p[3] = 255;
        continue;
      }
      if (hud.reticle && hud_erase::reticle(x, y) &&
          hud_erase::erase_pixel(hud.reticle_from_writer, native_erase,
                                 reticle_writer_erase ? 0 : rgb[y * W + x],
                                 hud.reticle_rgb)) {
        const uint32_t bg = reticle_writer_erase ?
            pal[size_t(y) * 32 + 5] : rgb[y * W + 165];
        p[0] = uint8_t(bg >> 16);
        p[1] = uint8_t(bg >> 8);
        p[2] = uint8_t(bg);
        p[3] = 255;
        continue;
      }
      if (frame_crown_source_pixel(x, y, hud,
                                   frame_writer_erase ? 0 : rgb[y * W + x],
                                   native_erase)) {
        const uint32_t bg = frame_writer_erase ?
            pal[size_t(y) * 32 + 5] : frame_crown_background(rgb, x, y);
        p[0] = uint8_t(bg >> 16);
        p[1] = uint8_t(bg >> 8);
        p[2] = uint8_t(bg);
        p[3] = 255;
        continue;
      }
      if (frame_rail_source_pixel(x, y, hud,
                                  frame_writer_erase ? 0 : rgb[y * W + x],
                                  native_erase)) {
        if (flight && y < 138 && y < ready->rows) {
          // Reveal native scene indices, using the same double-buffered row
          // lookup as every other scene-backed pixel in the GPU presenter.
          p[0] = p[1] = p[2] = p[3] = 0;
        } else {
          const auto asset = rail_asset_enabled ?
              cockpit_rail::asset_background(cockpit, pal, x, y) : std::nullopt;
          const uint32_t bg = asset ? *asset : frame_rail_cockpit_background(rgb, x, y);
          if (asset) ++rail_asset_pixels;
          p[0] = uint8_t(bg >> 16);
          p[1] = uint8_t(bg >> 8);
          p[2] = uint8_t(bg);
          p[3] = 255;
        }
        continue;
      }
      if (fuel.contains(x, y)) {
        const uint32_t c = fuel.backing(x, y);
        p[0] = uint8_t(c >> 16);
        p[1] = uint8_t(c >> 8);
        p[2] = uint8_t(c);
        p[3] = 255;
        continue;
      }
      if (readouts.contains(x, y)) {
        if (const auto* digit = readouts.digit(x, y)) {
          // The direct GPU presenter decodes this inline token without an
          // eighth sampler: RGB = stroke mask, local origin X/Y; A = 96 for
          // green LCD, 112 for black-on-purple ECM backing.
          p[0] = digit->strokes;
          p[1] = uint8_t(digit->x - FlightReadouts::X);
          p[2] = uint8_t(digit->y - FlightReadouts::Y);
          p[3] = digit->background_pen == 5 ? 112 : 96;
        } else {
          const uint32_t c = readouts.pixel(x, y);
          p[0] = uint8_t(c >> 16);
          p[1] = uint8_t(c >> 8);
          p[2] = uint8_t(c);
          p[3] = 255;
        }
        continue;
      }
      if (front_asset_enabled && cockpit_front::painted(cockpit, x, y)) {
        // Alpha 32 selects the ADF cockpit texture plus the displayed copper
        // palette in the visible GL shader; no source bitplane comparison.
        p[0] = p[1] = p[2] = 0;
        p[3] = 32;
        ++front_asset_pixels;
        continue;
      }
      if (const auto pen = projected_globe_pen(globe, cockpit, x, y)) {
        const uint32_t c = pal[size_t(y) * 32 + *pen];
        p[0] = uint8_t(c >> 16);
        p[1] = uint8_t(c >> 8);
        p[2] = uint8_t(c);
        p[3] = 255;
        continue;
      }
      if (panel_asset_enabled && cockpit_panel::painted(cockpit, x, y)) {
        // Same native indexed asset token as the front cockpit, resolved
        // through the *live* scanline copper palette in the GL shader.
        p[0] = p[1] = p[2] = 0;
        p[3] = 32;
        ++panel_asset_pixels;
        continue;
      }
      const size_t cp = size_t(y) * W + x;
      if (cockpit_enabled && cockpit.valid[cp]) {
        const bool index_matches = li == cockpit.index[cp];
        const bool colour_matches = (rgb[cp] & 0xFFFFFFu) == (cockpit.rgb[cp] & 0xFFFFFFu);
        if (index_matches || colour_matches) {
          // Alpha 32/255 selects the indexed artwork with the current copper
          // palette; 48/255 uses the ILBM's RGB when the screen palette has
          // remapped those colour indices. Both reproduce the source pixel.
          p[0] = p[1] = p[2] = 0;
          p[3] = index_matches ? 32 : 48;
          ++static_pixels;
          continue;
        }
        ++dynamic_pixels;
      }
      const bool missing_3d_row = flight && !paused && y < ready->rows && !scene;
      if (missing_3d_row ||
          (scene && li % 16 == L->snap[ci] && !L->unexplained[ci])) {
        p[0] = p[1] = p[2] = 0;
        p[3] = !missing_3d_row && (li & 16) ? 16 : 0;
      } else {
        const uint32_t c = rgb[y * W + x];
        p[0] = uint8_t(c >> 16);
        p[1] = uint8_t(c >> 8);
        p[2] = uint8_t(c);
        p[3] = 255;
      }
    }
  }
  if (std::getenv("FA18_COCKPIT_DEBUG") && static_pixels > 10000) {
    static bool reported = false;
    if (!reported) {
      reported = true;
      std::fprintf(stderr, "cockpit: frame %llu native static=%d dynamic/fallback=%d\n",
                   (unsigned long long)m.frame_count(), static_pixels, dynamic_pixels);
    }
  }
  if (std::getenv("FA18_HUD_RAIL_ART_DEBUG") && m.frame_count() % 400 == 0) {
    static uint64_t reported = UINT64_MAX;
    if (reported != m.frame_count()) {
      reported = m.frame_count();
      std::fprintf(stderr, "HUD rail art GPU frame=%llu pixels=%d\n",
                   (unsigned long long)m.frame_count(), rail_asset_pixels);
    }
  }
  if (std::getenv("FA18_COCKPIT_FRONT_DEBUG") && m.frame_count() % 400 == 0) {
    static uint64_t reported = UINT64_MAX;
    if (reported != m.frame_count()) {
      reported = m.frame_count();
      std::fprintf(stderr, "cockpit front GPU frame=%llu pixels=%d\n",
                   (unsigned long long)m.frame_count(), front_asset_pixels);
    }
  }
  if (std::getenv("FA18_COCKPIT_PANEL_DEBUG") && m.frame_count() % 400 == 0) {
    static uint64_t reported = UINT64_MAX;
    if (reported != m.frame_count()) {
      reported = m.frame_count();
      std::fprintf(stderr, "cockpit panel GPU frame=%llu pixels=%d\n",
                   (unsigned long long)m.frame_count(), panel_asset_pixels);
    }
  }
  return true;
}

bool pause_screen_active() {
  const uint32_t state = native::rd32(0x2a6d4);
  return state == 0x23b7e || state == 0x23ba8;
}

// In the original cockpit, each font reference point has a fixed *palette
// index* beneath it, even though the corresponding copper RGB values fade.
// These indices were checked against the unmodified screen-bank bitplanes:
// pen 5 behind upper text and heading, pen 12 behind the left unit/G-meter,
// and pen 4 beneath the right unit labels. An unknown position cannot act as
// an oracle; it must keep the original bitmap fallback.
static int hud_backdrop_pen(int y, int x) {
  if (y == 53 && (x == 135 || x == 156)) return 5;
  if (y == 63 && x == 165) return 5;
  if (y == 73 && x == 165) return 5;
  if (y == 84 && (x == 94 || x == 215)) return 5;
  if (y == 91 && x == 94) return 12;
  if (y == 91 && x == 215) return 4;
  if (y == 121 && x == 93) return 12;
  return -1;
}

// Once the original 68000 font writer has supplied the glyph, its stroke
// uses copper pen 13. Neither foreground nor backdrop requires a bitmap
// pixel read on the production capture path.
static bool hud_font_palette_ink(const amiga::Machine& m, int y, int background_x,
                                 uint32_t& ink) {
  const uint32_t* palette = m.frame_row_pal();
  const int bg_pen = hud_backdrop_pen(y, background_x);
  if (!palette || bg_pen < 0) return false;
  ink = palette[size_t(y) * 32 + 13] & 0xffffffu;
  uint32_t background = palette[size_t(y) * 32 + size_t(bg_pen)] & 0xffffffu;
  if (std::getenv("FA18_HUD_BACKDROP_REFERENCE")) {
    const uint32_t* rgb = m.frame_rgb();
    if (!rgb) return false;
    background = rgb[y * W + background_x] & 0xffffffu;
  }
  return hud_font::glyph_ink(ink, background);
}

// The aiming reticle uses pen 10, not the heading/font's pen 13. Its fade
// reaches 0x111111 in Pause, so use its original >=16 luminance threshold
// rather than the brighter 3x5 glyph contrast rule.
static bool hud_reticle_palette_ink(const amiga::Machine& m, uint32_t& ink) {
  const uint32_t* palette = m.frame_row_pal();
  if (!palette) return false;
  ink = palette[73 * 32 + 10] & 0xffffffu;
  uint32_t background = palette[73 * 32 + 5] & 0xffffffu;
  if (std::getenv("FA18_HUD_BACKDROP_REFERENCE")) {
    const uint32_t* rgb = m.frame_rgb();
    if (!rgb) return false;
    background = rgb[73 * W + 165] & 0xffffffu;
  }
  const uint32_t r = (ink >> 16) & 255u;
  return ink != background && r >= 16 && ink == r * 0x010101u;
}

NativeHudState native_hud_state(const amiga::Machine& m) {
  NativeHudState out;
  const uint32_t* rgb = m.frame_rgb();
  if (!rgb) return out;

  const bool paused = pause_screen_active();
  observe_pause_title_state(paused);
  if (paused) {
    // The original enters the Pause state a few frames before drawing its
    // title. Track its original 5x7 font writes per displayed screen bank;
    // require the same minimum ink coverage as the previous pixel scan.
    const int painted = captured_pause_title_pixels(m.frame_row_key(), m.frame_count());
    const bool from_writer = !std::getenv("FA18_PAUSE_TITLE_BITMAP_REFERENCE");
    int amber_pixels = 0;
    if (!from_writer || std::getenv("FA18_PAUSE_TITLE_CAPTURE_DEBUG")) {
      for (int y = 42; y <= 50; ++y)
        for (int x = 124; x <= 201; ++x) {
          const uint32_t c = rgb[y * W + x];
          const int r = int((c >> 16) & 255), g = int((c >> 8) & 255);
          if (r >= 32 && g >= 16 && r > g && g > int(c & 255)) ++amber_pixels;
        }
    }
    out.pause_title = (from_writer ? painted : amber_pixels) > 24;
    if (std::getenv("FA18_PAUSE_TITLE_CAPTURE_DEBUG") &&
        m.frame_count() >= 3540 && m.frame_count() <= 3590)
      std::fprintf(stderr, "Pause title capture use frame=%llu writer=%d painted=%d bitmap=%d visible=%d\n",
                   (unsigned long long)m.frame_count(), int(from_writer),
                   painted, amber_pixels, int(out.pause_title));
    // The Pause heading's original orange pixels use displayed pen 9 on
    // scanline 44 (verified against the bitplane frame). The captured title
    // pixel count gates visibility; the copper palette follows its fade.
    if (out.pause_title) {
      const uint32_t* pal = m.frame_row_pal();
      out.pause_rgb = pal ? (pal[44 * 32 + 9] & 0xffffffu) : 0x884400u;
    }
  }

  // Top and corner strokes are two different original pens. A recently
  // completed five-line outline draw establishes visibility for the active
  // screen bank; the copper palette supplies its faded colours. External
  // camera, partial redraw and transitional frames keep the pixel stencil.
  const char* native_frame = std::getenv("FA18_HUD_FRAME_NATIVE");
  if (!native_frame || std::strcmp(native_frame, "0") != 0) {
    const uint64_t drawn = captured_hud_frame(m.frame_row_key());
    const uint32_t* row_palette = m.frame_row_pal();
    const bool observed = drawn != UINT64_MAX && m.frame_count() >= drawn;
    const bool fresh = observed && m.frame_count() - drawn <= 16;
    const bool use_writer = std::getenv("FA18_HUD_FRAME_BITMAP_REFERENCE") == nullptr &&
                            (fresh || (paused && observed)) && row_palette &&
                            (native::rd8(0x5aa8d) != 0 || paused);
    if (use_writer) {
      const uint32_t top = row_palette[37 * 32 + 10] & 0xffffffu;
      const uint32_t corner = row_palette[38 * 32 + 8] & 0xffffffu;
      const uint32_t backdrop = row_palette[36 * 32 + 5] & 0xffffffu;
      const uint8_t r = uint8_t(top >> 16);
      out.frame_crown = r >= 16 && top == uint32_t(r) * 0x010101u &&
                        top != backdrop;
      out.frame_top_rgb = top;
      out.frame_corner_rgb = corner;
    } else {
      const uint32_t top = rgb[37 * W + 159] & 0xffffffu;
      const uint32_t corner = rgb[38 * W + 92] & 0xffffffu;
      const uint8_t r = uint8_t(top >> 16);
      out.frame_crown = r >= 16 && top == (uint32_t(r) * 0x010101u) &&
                        top != (rgb[36 * W + 159] & 0xffffffu) &&
                        (rgb[37 * W + 94] & 0xffffffu) == top &&
                        (rgb[37 * W + 200] & 0xffffffu) == top &&
                        (rgb[38 * W + 226] & 0xffffffu) == corner &&
                        (rgb[45 * W + 85] & 0xffffffu) == corner &&
                        (rgb[45 * W + 233] & 0xffffffu) == corner;
      out.frame_top_rgb = top;
      out.frame_corner_rgb = corner;
    }
    out.frame_from_writer = use_writer && out.frame_crown;
    const char* rails = std::getenv("FA18_HUD_RAILS_NATIVE");
    if (out.frame_crown && (!rails || std::strcmp(rails, "0") != 0)) {
      const uint32_t ink = use_writer ? row_palette[55 * 32] & 0xffffffu :
                                        rgb[55 * W + 85] & 0xffffffu;
      out.frame_rails = use_writer ?
          ((row_palette[100 * 32] & 0xffffffu) == ink &&
           (row_palette[140 * 32] & 0xffffffu) == ink) :
          ((rgb[55 * W + 233] & 0xffffffu) == ink &&
           (rgb[100 * W + 85] & 0xffffffu) == ink &&
           (rgb[100 * W + 233] & 0xffffffu) == ink &&
           (rgb[140 * W + 85] & 0xffffffu) == ink &&
           (rgb[140 * W + 233] & 0xffffffu) == ink);
      out.frame_rail_rgb = ink;
    }
    if (std::getenv("FA18_HUD_FRAME_CAPTURE_DEBUG") &&
        (m.frame_count() % 400 == 0 || (paused && m.frame_count() % 20 == 10)))
      std::fprintf(stderr, "HUD frame capture use frame=%llu writer=%d visible=%d rails=%d\n",
                   (unsigned long long)m.frame_count(), int(use_writer),
                   int(out.frame_crown), int(out.frame_rails));
  }

  if (out.frame_crown) {
    const HudFontCapture captured = captured_hud_font(m.frame_row_key());
    const bool palette_native = std::getenv("FA18_HUD_PALETTE_REFERENCE") == nullptr;
    const bool point_palette_native = std::getenv("FA18_HUD_POINT_PALETTE_REFERENCE") == nullptr;
    int captured_digits = 0, captured_letters = 0;
    const char* marks = std::getenv("FA18_HUD_HEADING_NATIVE");
    if (!marks || std::strcmp(marks, "0") != 0) {
      const int captured_phase = captured_heading_phase(m.frame_row_key());
      const bool from_writer = captured_phase >= 0 &&
          (point_palette_native ? hud_font_palette_ink(m, 63, 165, out.heading_rgb) :
                                  hud_font::live_heading_ink(rgb, captured_phase,
                                                             out.heading_rgb));
      if (from_writer) {
        out.heading_marks = true;
        out.heading_first_x = captured_phase;
        out.heading_from_writer = point_palette_native && out.frame_from_writer;
      } else {
        out.heading_marks = hud_font::decode_heading_marks(
            rgb, out.heading_first_x, out.heading_rgb);
      }
      if (std::getenv("FA18_HUD_MARK_CAPTURE_DEBUG") &&
          m.frame_count() % 400 == 0 && out.heading_marks) {
        static uint64_t last_report = UINT64_MAX;
        if (last_report != m.frame_count()) {
          last_report = m.frame_count();
          std::fprintf(stderr, "HUD heading capture use frame=%llu writer=%d phase=%d\n",
                       (unsigned long long)m.frame_count(), int(from_writer),
                       out.heading_first_x);
        }
      }
      if (out.heading_marks && std::getenv("FA18_HUD_DIGITS_DEBUG")) {
        static int reported = 0;
        if (reported++ < 24)
          std::fprintf(stderr, "HUD heading frame=%llu tick=%d ink=%06x\n",
                       (unsigned long long)m.frame_count(),
                       out.heading_first_x, out.heading_rgb);
      }
    }
    const char* digits = std::getenv("FA18_HUD_DIGITS_NATIVE");
    if (!digits || std::strcmp(digits, "0") != 0) {
      for (size_t i = 0; i < kHudDigitSlots.size(); ++i) {
        uint32_t ink = 0;
        int value = captured.digits[i];
        const auto& slot = kHudDigitSlots[i];
        const bool palette_capture = palette_native && value >= 0 && value < 10 &&
            hud_font_palette_ink(m, slot.y, slot.background_x, ink);
        const bool captured_ink = palette_native ? palette_capture :
            (value >= 0 && value < 10 &&
             hud_font::live_ink(rgb, slot.x, slot.y, slot.background_x,
                                hud_font::kDigitRows[value][0], ink));
        if (captured_ink) {
          ++captured_digits;
          if (palette_native && out.frame_from_writer)
            out.digit_writer_value[i] = uint8_t(value + 1);
        } else {
          value = hud_font::decode_digit(rgb, kHudDigitSlots[i], ink);
        }
        if (value < 0) continue;
        out.digit_strokes[i] = hud_font::kDigitStrokes[value];
        out.digit_colours[kHudDigitSlots[i].group] = ink;
        if (std::getenv("FA18_HUD_DIGITS_DEBUG")) {
          static int reported = 0;
          if (reported++ < 48)
            std::fprintf(stderr, "HUD digit frame=%llu x=%d y=%d value=%d ink=%06x\n",
                         (unsigned long long)m.frame_count(),
                         kHudDigitSlots[i].x, kHudDigitSlots[i].y, value, ink);
        }
      }
      const char* letters = std::getenv("FA18_HUD_LABELS_NATIVE");
      if (!letters || std::strcmp(letters, "0") != 0) {
        for (size_t i = 0; i < kHudLetterSlots.size(); ++i) {
          uint32_t ink = 0;
          const auto& slot = kHudLetterSlots[i];
          const int glyph = hud_font::letter_index(slot.letter);
          const bool palette_capture = palette_native && captured.letters[i] && glyph >= 0 &&
              hud_font_palette_ink(m, slot.y, slot.background_x, ink);
          const bool drawn = palette_native ? palette_capture :
              (captured.letters[i] && glyph >= 0 &&
               hud_font::live_ink(rgb, slot.x, slot.y, slot.background_x,
                                  hud_font::kLetterRows[glyph][0], ink));
          if (drawn) {
            ++captured_letters;
          } else if (!hud_font::decode_letter(rgb, slot, ink)) {
            continue;
          }
          out.letter_visible[i] = 1;
          out.letter_from_writer[i] = drawn && palette_native && out.frame_from_writer;
          out.letter_colours[i] = ink;
          if (std::getenv("FA18_HUD_DIGITS_DEBUG")) {
            static int reported = 0;
            if (reported++ < 32)
              std::fprintf(stderr, "HUD label frame=%llu x=%d y=%d letter=%c ink=%06x\n",
                           (unsigned long long)m.frame_count(),
                           kHudLetterSlots[i].x, kHudLetterSlots[i].y,
                           kHudLetterSlots[i].letter, ink);
          }
        }
      }
      if (std::getenv("FA18_HUD_CAPTURE_DEBUG") && m.frame_count() % 400 == 0)
        std::fprintf(stderr, "HUD capture use frame=%llu digits=%d letters=%d\n",
                     (unsigned long long)m.frame_count(), captured_digits, captured_letters);
    }
  }

  if (std::getenv("FA18_HUD_TEXT_ERASE_DEBUG") && m.frame_count() % 400 == 0) {
    static uint64_t reported = UINT64_MAX;
    if (reported != m.frame_count()) {
      reported = m.frame_count();
      const int digits = int(std::count_if(out.digit_writer_value.begin(),
                                           out.digit_writer_value.end(),
                                           [](uint8_t value) { return value != 0; }));
      const int letters = int(std::count_if(out.letter_from_writer.begin(),
                                            out.letter_from_writer.end(),
                                            [](uint8_t value) { return value != 0; }));
      std::fprintf(stderr,
                   "HUD text erase capture frame=%llu digits=%d letters=%d heading=%d\n",
                   (unsigned long long)m.frame_count(), digits, letters,
                   int(out.heading_from_writer));
    }
  }

  if (const char* opt = std::getenv("FA18_HUD_RETICLE_NATIVE");
      opt && std::strcmp(opt, "0") == 0) return out;
  // The four vertical points and horizontal bar are recorded directly from
  // the original 68000 write commands for the displayed screen bank. Once
  // complete, pen 10 supplies its colour without reading a reticle glyph
  // pixel. The flight HUD frame and backdrop still gate visibility; until
  // a bank has a complete redraw, keep the prior exact bitmap recogniser.
  const bool point_palette_native = std::getenv("FA18_HUD_POINT_PALETTE_REFERENCE") == nullptr;
  const bool from_writer = captured_reticle_ready(m.frame_row_key()) &&
      (point_palette_native ? (out.frame_crown &&
                               hud_reticle_palette_ink(m, out.reticle_rgb)) :
                              hud_font::live_reticle_ink(rgb, out.reticle_rgb));
  if (from_writer) {
    out.reticle = true;
    // A reticle latch can outlive the flight outline. Only replace its
    // bitmap footprint unconditionally while the current bank also has a
    // verified, recent (or Pause-held) complete HUD frame.
    out.reticle_from_writer = out.frame_from_writer;
  } else {
    const uint32_t ink = rgb[73 * W + 159] & 0xffffffu;
    const uint32_t backdrop = rgb[73 * W + 165] & 0xffffffu;
    const uint8_t r = uint8_t(ink >> 16), g = uint8_t(ink >> 8), b = uint8_t(ink);
    out.reticle = ink != backdrop && r >= 16 && r == g && g == b &&
                  (rgb[71 * W + 159] & 0xffffffu) == ink &&
                  (rgb[73 * W + 157] & 0xffffffu) == ink &&
                  (rgb[73 * W + 161] & 0xffffffu) == ink;
    out.reticle_rgb = ink;
  }
  if (std::getenv("FA18_HUD_RETICLE_CAPTURE_DEBUG") &&
      m.frame_count() % 400 == 0 && out.reticle) {
    static uint64_t last_report = UINT64_MAX;
    if (last_report != m.frame_count()) {
      last_report = m.frame_count();
      std::fprintf(stderr, "HUD reticle capture use frame=%llu writer=%d\n",
                   (unsigned long long)m.frame_count(), int(from_writer));
    }
  }
  return out;
}

NativeHudState prepared_gpu_hud_state() { return last_prepared_gpu_hud; }

}  // namespace remaster
