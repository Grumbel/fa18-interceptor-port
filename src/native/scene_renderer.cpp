#include "native/scene_renderer.hpp"
#include "amiga/machine.hpp"
#include "native/runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace scene {
namespace {

constexpr int W = 320;
constexpr int H = 200;

SoftwareSceneRenderer* g_soft = nullptr;
bool g_checked = false;

uint32_t colour_for(uint16_t mesh_id, PrimKind kind) {
  uint32_t x = uint32_t(mesh_id) * 0x9E3779B9u + 0x7F4A7C15u;
  x ^= x >> 16;
  uint8_t r = uint8_t(80 + ((x >> 0) & 127));
  uint8_t g = uint8_t(80 + ((x >> 8) & 127));
  uint8_t b = uint8_t(80 + ((x >> 16) & 127));
  if (kind == PrimKind::Line) {
    r = uint8_t(std::min(255, int(r) + 40));
    g = uint8_t(std::min(255, int(g) + 40));
    b = uint8_t(std::min(255, int(b) + 40));
  } else if (kind == PrimKind::Point) {
    return 0xFFFFFFFFu;
  }
  return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

struct V3 { double x, y, z; };
struct V2 { double x, y; };

inline V2 project(const V3& v) {
  return {159.0 - 160.0 * v.x / v.z, 89.0 - 90.0 * v.y / v.z};
}

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

void put(std::vector<uint32_t>& px, int Wp, int Hp, int x, int y, uint32_t c) {
  if (x < 0 || y < 0 || x >= Wp || y >= Hp) return;
  px[size_t(y) * size_t(Wp) + size_t(x)] = c;
}

void draw_line(std::vector<uint32_t>& px, int S, int Wp, int Hp, V2 a, V2 b, uint32_t c) {
  double dx = (b.x - a.x) * S, dy = (b.y - a.y) * S;
  int n = int(std::ceil(std::max(std::fabs(dx), std::fabs(dy)))) + 1;
  for (int i = 0; i <= n; ++i) {
    double t = n ? double(i) / n : 0.0;
    int px_ = int(std::floor((a.x + (b.x - a.x) * t) * S));
    int py_ = int(std::floor((a.y + (b.y - a.y) * t) * S));
    for (int yy = 0; yy < S; ++yy)
      for (int xx = 0; xx < S; ++xx)
        put(px, Wp, Hp, px_ - S / 2 + xx, py_ - S / 2 + yy, c);
  }
}

void fill_polygon(std::vector<uint32_t>& px, int S, int Wp, int Hp,
                  const std::vector<V2>& pts, uint32_t c) {
  if (pts.size() < 3) return;
  double ymin = 1e30, ymax = -1e30;
  for (auto& p : pts) {
    ymin = std::min(ymin, p.y);
    ymax = std::max(ymax, p.y);
  }
  int y0 = std::max(0, int(std::floor(ymin * S)));
  int y1 = std::min(Hp - 1, int(std::ceil(ymax * S)));
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
      for (int x = xa; x <= xb; ++x) put(px, Wp, Hp, x, y, c);
    }
  }
}

void draw_prim(std::vector<uint32_t>& px, int S, int Wp, int Hp,
               const Prim& p, uint16_t mesh_id) {
  uint32_t c = p.rgba ? p.rgba : colour_for(mesh_id, p.kind);
  if (p.kind == PrimKind::Poly3d) {
    if (p.n < 3) return;
    int shift = int(p.shift) & 31;
    std::vector<V3> pts;
    pts.reserve(p.n);
    for (int i = 0; i < p.n; ++i) {
      double sx = double(int32_t(uint32_t(int32_t(p.xyz[i][0])) << shift));
      double sy = double(int32_t(uint32_t(int32_t(p.xyz[i][1])) << shift));
      double sz = double(int32_t(uint32_t(int32_t(p.xyz[i][2])) << shift));
      pts.push_back({sx, sy, sz});
    }
    auto clipped = clip_near(pts, 1.0);
    if (clipped.size() < 3) return;
    std::vector<V2> sp;
    sp.reserve(clipped.size());
    for (auto& v : clipped) sp.push_back(project(v));
    fill_polygon(px, S, Wp, Hp, sp, c);
  } else if (p.kind == PrimKind::Line) {
    if (p.n < 2) return;
    V3 a{double(p.xyz[0][0]), double(p.xyz[0][1]), double(p.xyz[0][2])};
    V3 b{double(p.xyz[1][0]), double(p.xyz[1][1]), double(p.xyz[1][2])};
    auto clipped = clip_near({a, b}, 1.0);
    if (clipped.size() < 2) return;
    draw_line(px, S, Wp, Hp, project(clipped[0]), project(clipped[1]), c);
  } else if (p.kind == PrimKind::Point) {
    if (p.n < 1) return;
    V3 v{double(p.xyz[0][0]), double(p.xyz[0][1]), double(p.xyz[0][2])};
    if (v.z <= 0) return;
    V2 sp = project(v);
    double cx = sp.x + 0.5, cy = sp.y + 0.5 + 1.0;
    int size = 1;
    if (p.sel <= -2) size = 2;
    int x0 = int(std::floor(cx * S - size * S / 2.0));
    int y0 = int(std::floor(cy * S - size * S / 2.0));
    for (int yy = 0; yy < size * S; ++yy)
      for (int xx = 0; xx < size * S; ++xx)
        put(px, Wp, Hp, x0 + xx, y0 + yy, c);
  } else if (p.kind == PrimKind::Fill2d) {
    if (p.n < 3) return;
    std::vector<V2> sp;
    for (int i = 0; i < p.n; ++i)
      sp.push_back({double(p.xyz[i][0]) + 0.5, double(p.xyz[i][1]) + 0.5});
    fill_polygon(px, S, Wp, Hp, sp, c);
  }
}

int32_t prim_depth(const Prim& p) {
  if (p.kind == PrimKind::Fill2d) return 0x7fffffff;  // draw first (far)
  if (p.n <= 0) return 0;
  int64_t sum = 0;
  int shift = int(p.shift) & 31;
  for (int i = 0; i < p.n; ++i) {
    int32_t z = p.xyz[i][2];
    if (p.kind == PrimKind::Poly3d) z = int32_t(uint32_t(z) << shift);
    sum += z;
  }
  return int32_t(sum / p.n);
}

}  // namespace

SoftwareSceneRenderer::SoftwareSceneRenderer(int scale)
    : scale_(std::max(1, std::min(8, scale))) {}

void SoftwareSceneRenderer::on_scene(const SceneFrame& frame) {
  rows_ = 200;
  if (native::g_machine) {
    int vr = int(native::rd16(0x5ac7c)) + 1;
    if (vr > 0 && vr <= 200) rows_ = vr;
  }
  width_ = W * scale_;
  height_ = rows_ * scale_;

  // Clear the entire 3D view.  Unwritten pixels stay this colour — if horizon
  // or a mesh is missing from the capture, you see the clear colour, not a
  // low-res composite hiding the gap.
  uint32_t clear = 0xFF101018u;
  if (native::g_machine) {
    uint16_t c12 = native::g_machine->color_reg(0);
    uint32_t r = (c12 >> 8) & 0xF, g = (c12 >> 4) & 0xF, b = c12 & 0xF;
    r |= r << 4; g |= g << 4; b |= b << 4;
    clear = 0xFF000000u | (r << 16) | (g << 8) | b;
  }
  pixels_.assign(size_t(width_) * size_t(height_), clear);

  struct DrawItem {
    const Prim* prim;
    uint16_t mesh_id;
    int32_t depth;
  };
  std::vector<DrawItem> items;
  items.reserve(frame.prims.size());
  for (const Prim& p : frame.prims) {
    uint16_t mid = 0;
    int32_t depth = prim_depth(p);
    if (p.object_index >= 0 && size_t(p.object_index) < frame.objects.size()) {
      const ObjectDraw& o = frame.objects[size_t(p.object_index)];
      mid = o.mesh_id;
      if (depth == 0 && o.distance != 0) depth = o.distance;
    }
    items.push_back({&p, mid, depth});
  }
  std::stable_sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b) {
    return a.depth > b.depth;  // far → near
  });
  for (const DrawItem& it : items)
    draw_prim(pixels_, scale_, width_, height_, *it.prim, it.mesh_id);

  last_frame_id_ = frame.frame_id;
  has_frame_ = true;

  if (std::getenv("FA18_SCENE_RENDER_STATS")) {
    size_t fills = 0, polys = 0, lines = 0, points = 0;
    for (const Prim& p : frame.prims) {
      switch (p.kind) {
        case PrimKind::Fill2d: ++fills; break;
        case PrimKind::Poly3d: ++polys; break;
        case PrimKind::Line: ++lines; break;
        case PrimKind::Point: ++points; break;
      }
    }
    std::fprintf(stderr,
                 "[scene-render] frame %llu objects=%zu prims=%zu "
                 "(fill=%zu poly=%zu line=%zu point=%zu) scale=%d %dx%d\n",
                 (unsigned long long)frame.frame_id, frame.objects.size(),
                 frame.prims.size(), fills, polys, lines, points,
                 scale_, width_, height_);
  }
}

void ensure_software_renderer() {
  if (g_checked) return;
  g_checked = true;
  const char* e = std::getenv("FA18_SCENE_RENDER");
  if (!e || !*e) return;
  int scale = std::atoi(e);
  if (scale <= 0) scale = 1;
  static SoftwareSceneRenderer instance(scale);
  g_soft = &instance;
  set_renderer(g_soft);
}

bool software_render_enabled() { return g_soft != nullptr; }

bool compose_software(std::vector<uint32_t>& out, int& w, int& h) {
  if (!g_soft || !g_soft->has_frame() || !native::g_machine) return false;

  const int S = g_soft->scale();
  w = W * S;
  const int out_h = H * S;
  out.assign(size_t(w) * size_t(out_h), 0xFF000000u);

  const uint32_t* low = native::g_machine->frame_rgb();
  if (!low) return false;

  const int scene_h = g_soft->height();  // hi-res rows of the 3D view
  const int view_rows = scene_h / S;     // low-res rows owned by the 3D view

  // 3D view: the software-rendered SceneFrame only — no low-res mesh under it.
  const auto& px = g_soft->pixels();
  for (int y = 0; y < scene_h; ++y) {
    std::memcpy(&out[size_t(y) * size_t(w)],
                &px[size_t(y) * size_t(w)],
                size_t(w) * sizeof(uint32_t));
  }

  // Below the view: HUD / cockpit / menus from the original low-res frame.
  for (int y = view_rows; y < H; ++y) {
    for (int yy = 0; yy < S; ++yy) {
      uint32_t* row = &out[size_t(y * S + yy) * size_t(w)];
      for (int x = 0; x < W; ++x) {
        uint32_t c = low[size_t(y) * W + size_t(x)];
        for (int xx = 0; xx < S; ++xx) row[x * S + xx] = c;
      }
    }
  }

  h = out_h;
  return true;
}

}  // namespace scene
