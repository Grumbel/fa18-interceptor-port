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

void put(std::vector<uint32_t>& px, std::vector<uint8_t>& cover,
         int Wp, int Hp, int x, int y, uint32_t c) {
  if (x < 0 || y < 0 || x >= Wp || y >= Hp) return;
  size_t i = size_t(y) * size_t(Wp) + size_t(x);
  px[i] = c;
  cover[i] = 1;
}

void draw_line(std::vector<uint32_t>& px, std::vector<uint8_t>& cover,
               int S, int Wp, int Hp, V2 a, V2 b, uint32_t c) {
  double dx = (b.x - a.x) * S, dy = (b.y - a.y) * S;
  int n = int(std::ceil(std::max(std::fabs(dx), std::fabs(dy)))) + 1;
  for (int i = 0; i <= n; ++i) {
    double t = n ? double(i) / n : 0.0;
    int px_ = int(std::floor((a.x + (b.x - a.x) * t) * S));
    int py_ = int(std::floor((a.y + (b.y - a.y) * t) * S));
    for (int yy = 0; yy < S; ++yy)
      for (int xx = 0; xx < S; ++xx)
        put(px, cover, Wp, Hp, px_ - S / 2 + xx, py_ - S / 2 + yy, c);
  }
}

void fill_polygon(std::vector<uint32_t>& px, std::vector<uint8_t>& cover,
                  int S, int Wp, int Hp, const std::vector<V2>& pts, uint32_t c) {
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
      for (int x = xa; x <= xb; ++x) put(px, cover, Wp, Hp, x, y, c);
    }
  }
}

void draw_prim(std::vector<uint32_t>& px, std::vector<uint8_t>& cover,
               int S, int Wp, int Hp, const Prim& p, uint16_t mesh_id) {
  uint32_t c = colour_for(mesh_id, p.kind);
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
    fill_polygon(px, cover, S, Wp, Hp, sp, c);
  } else if (p.kind == PrimKind::Line) {
    if (p.n < 2) return;
    V3 a{double(p.xyz[0][0]), double(p.xyz[0][1]), double(p.xyz[0][2])};
    V3 b{double(p.xyz[1][0]), double(p.xyz[1][1]), double(p.xyz[1][2])};
    auto clipped = clip_near({a, b}, 1.0);
    if (clipped.size() < 2) return;
    draw_line(px, cover, S, Wp, Hp, project(clipped[0]), project(clipped[1]), c);
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
        put(px, cover, Wp, Hp, x0 + xx, y0 + yy, c);
  } else if (p.kind == PrimKind::Fill2d) {
    if (p.n < 3) return;
    std::vector<V2> sp;
    for (int i = 0; i < p.n; ++i)
      sp.push_back({double(p.xyz[i][0]) + 0.5, double(p.xyz[i][1]) + 0.5});
    fill_polygon(px, cover, S, Wp, Hp, sp, c);
  }
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
  // Transparent clear: only covered pixels carry 3D colour.
  pixels_.assign(size_t(width_) * size_t(height_), 0);
  cover_.assign(size_t(width_) * size_t(height_), 0);

  std::vector<int> order;
  order.reserve(frame.objects.size());
  for (size_t i = 0; i < frame.objects.size(); ++i) order.push_back(int(i));
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
    return frame.objects[size_t(a)].distance > frame.objects[size_t(b)].distance;
  });

  std::vector<std::vector<const Prim*>> by_obj(frame.objects.size());
  std::vector<const Prim*> unscoped;
  for (const Prim& p : frame.prims) {
    if (p.object_index >= 0 && size_t(p.object_index) < frame.objects.size())
      by_obj[size_t(p.object_index)].push_back(&p);
    else
      unscoped.push_back(&p);
  }

  // Non-mesh screen-space fills (horizon, sky, ground, sea) are left to the
  // low-res composite by default — they are not triangle meshes and re-drawing
  // them with mesh_id colours looks wrong.  FA18_SCENE_RENDER_FILLS=1 forces
  // them through the software path for debugging.
  const bool draw_fills = std::getenv("FA18_SCENE_RENDER_FILLS") != nullptr;

  for (int oi : order) {
    const ObjectDraw& o = frame.objects[size_t(oi)];
    for (const Prim* pp : by_obj[size_t(oi)]) {
      if (pp->kind == PrimKind::Fill2d && !draw_fills) continue;
      draw_prim(pixels_, cover_, scale_, width_, height_, *pp, o.mesh_id);
    }
  }
  for (const Prim* pp : unscoped) {
    if (pp->kind == PrimKind::Fill2d && !draw_fills) continue;
    draw_prim(pixels_, cover_, scale_, width_, height_, *pp, 0);
  }

  last_frame_id_ = frame.frame_id;
  has_frame_ = true;

  if (std::getenv("FA18_SCENE_RENDER_STATS")) {
    size_t covered = 0;
    for (uint8_t c : cover_) covered += c != 0;
    std::fprintf(stderr,
                 "[scene-render] frame %llu objects=%zu prims=%zu scale=%d "
                 "%dx%d covered=%zu sorted_far_to_near\n",
                 (unsigned long long)frame.frame_id, frame.objects.size(),
                 frame.prims.size(), scale_, width_, height_, covered);
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
  w = g_soft->width();
  h = g_soft->height();
  // Full frame height for HUD rows below the 3D view.
  const int out_h = H * S;
  out.assign(size_t(w) * size_t(out_h), 0xFF000000u);

  const uint32_t* low = native::g_machine->frame_rgb();
  if (!low) return false;

  // 1. Nearest-neighbour upsample of the original low-res frame (HUD, cockpit,
  //    menus, and the original 3D view as a fallback under the overlay).
  for (int y = 0; y < H; ++y) {
    for (int yy = 0; yy < S; ++yy) {
      uint32_t* row = &out[size_t(y * S + yy) * size_t(w)];
      for (int x = 0; x < W; ++x) {
        uint32_t c = low[size_t(y) * W + size_t(x)];
        for (int xx = 0; xx < S; ++xx) row[x * S + xx] = c;
      }
    }
  }

  // 2. Overlay depth-sorted 3D where the scene renderer covered a pixel.
  //    Only the view rows captured in on_scene (typically <= 200) are used.
  const auto& px = g_soft->pixels();
  const auto& cover = g_soft->cover();
  const int scene_h = g_soft->height();
  for (int y = 0; y < scene_h; ++y) {
    for (int x = 0; x < w; ++x) {
      size_t i = size_t(y) * size_t(w) + size_t(x);
      if (cover[i]) out[i] = px[i];
    }
  }

  h = out_h;
  return true;
}

}  // namespace scene
