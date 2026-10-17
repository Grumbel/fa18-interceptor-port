#include "mesh_viewer.hpp"

#include "amiga/hunk.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mesh_viewer {
namespace {

uint16_t u16(const uint8_t* p) {
  return uint16_t((p[0] << 8) | p[1]);
}
int16_t s16(const uint8_t* p) {
  return int16_t(u16(p));
}
bool is_opcode(uint16_t w) { return w >= 0x4000 && w <= 0xDFFF; }

void add_edge(Mesh& m, int a, int b) {
  if (a == b) return;
  if (a > b) std::swap(a, b);
  if (a < 0 || b >= int(m.vx.size())) return;
  for (auto& e : m.edges)
    if (e.first == a && e.second == b) return;
  m.edges.emplace_back(a, b);
}

// Wireframe silhouette: body verts (low |Y|) in XZ angular order, plus fins.
// $800C sublists are shared triangle strips for a solid mesh and look wrong
// as a sparse offline wireframe when applied across different models.
void build_silhouette_edges(Mesh& m) {
  const int n = int(m.vx.size());
  if (n < 3) return;
  struct Item {
    int i;
    float ang;
  };
  std::vector<Item> body;
  body.reserve(size_t(n));
  for (int i = 0; i < n; ++i) {
    if (std::abs(int(m.vy[size_t(i)])) <= 80)
      body.push_back({i, std::atan2(float(m.vz[size_t(i)]), float(m.vx[size_t(i)]))});
  }
  if (body.size() < 3) {
    // Fallback: sequential ring
    for (int i = 0; i < n; ++i) add_edge(m, i, (i + 1) % n);
    return;
  }
  std::sort(body.begin(), body.end(), [](const Item& a, const Item& b) { return a.ang < b.ang; });
  for (size_t i = 0; i < body.size(); ++i)
    add_edge(m, body[i].i, body[(i + 1) % body.size()].i);
  // Fins / verticals: high-|Y| verts → nearest body vert in XZ
  for (int i = 0; i < n; ++i) {
    if (std::abs(int(m.vy[size_t(i)])) <= 80) continue;
    int best = body[0].i;
    int best_d = INT_MAX;
    for (auto& b : body) {
      int dx = int(m.vx[size_t(i)]) - int(m.vx[size_t(b.i)]);
      int dz = int(m.vz[size_t(i)]) - int(m.vz[size_t(b.i)]);
      int d = dx * dx + dz * dz;
      if (d < best_d) {
        best_d = d;
        best = b.i;
      }
    }
    add_edge(m, i, best);
  }
}

Mesh* parse_record(std::vector<Mesh>& out, const uint8_t* data, size_t size,
                   size_t start, size_t end, int hunk_i) {
  if (end < start + 16) return nullptr;
  uint16_t t0 = u16(data + start), t1 = u16(data + start + 2);
  // Aircraft family: $0100/$0200 and $0100/$0300.  Other tags are different
  // asset classes (not yet decoded for offline verts).
  if (t0 != 0x0100 || (t1 != 0x0200 && t1 != 0x0300)) return nullptr;

  size_t off = start + 4;
  while (off + 2 <= end && u16(data + off) == 0) off += 2;

  // Hull: (x,y) pairs until word-aligned $FFFF $FFFF (not always pair-aligned
  // from the start — $0100/$0300 records can end mid-pair).
  bool hull_ended = false;
  while (off + 4 <= end) {
    if (u16(data + off) == 0xFFFF && u16(data + off + 2) == 0xFFFF) {
      off += 4;
      hull_ended = true;
      break;
    }
    // Prefer pair step; if next pair-start is past a FFFF FFFF, word-scan.
    if (off + 6 <= end && u16(data + off + 2) == 0xFFFF && u16(data + off + 4) == 0xFFFF) {
      off += 6;  // trailing half-pair + separator
      hull_ended = true;
      break;
    }
    uint16_t a = u16(data + off);
    if (is_opcode(a) && a != 0xFFFF) return nullptr;
    off += 4;
  }
  if (!hull_ended || off + 2 > end) return nullptr;

  Mesh m;
  m.tags0 = t0;
  m.tags1 = t1;
  m.declared_n = u16(data + off);
  off += 2;

  while (off + 6 <= end) {
    uint16_t w = u16(data + off);
    if (is_opcode(w)) break;
    int16_t x = s16(data + off), y = s16(data + off + 2), z = s16(data + off + 4);
    if (std::max({std::abs(int(x)), std::abs(int(y)), std::abs(int(z))}) > 3000) break;
    m.vx.push_back(x);
    m.vy.push_back(y);
    m.vz.push_back(z);
    off += 6;
  }
  if (m.vx.size() < 3) return nullptr;

  if (off + 4 <= end && u16(data + off) == 0x4070) {
    uint16_t nxt = u16(data + off + 2);
    if ((nxt & 0xFF00) == 0x4000) m.pp = int(nxt & 0xFF);
  }

  build_silhouette_edges(m);

  char buf[80];
  std::snprintf(buf, sizeof(buf), "H%d +$%zx  tags %04X/%04X  %zu verts  %zu edges",
                hunk_i, start, m.tags0, m.tags1, m.vx.size(), m.edges.size());
  m.label = buf;
  out.push_back(std::move(m));
  return &out.back();
}

void put_px(std::vector<uint32_t>& fb, int w, int h, int x, int y, uint32_t c) {
  if (x < 0 || y < 0 || x >= w || y >= h) return;
  fb[size_t(y) * size_t(w) + size_t(x)] = c;
}

void draw_line(std::vector<uint32_t>& fb, int w, int h, int x0, int y0, int x1, int y1,
               uint32_t c) {
  int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    put_px(fb, w, h, x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

}  // namespace

std::vector<Mesh> load_from_exe(const std::vector<uint8_t>& exe) {
  std::vector<Mesh> meshes;
  amiga::HunkFile hf;
  if (!hf.parse(exe)) return meshes;

  for (size_t hi = 0; hi < hf.hunks.size(); ++hi) {
    const auto& h = hf.hunks[hi];
    if (h.kind != amiga::Hunk::Code || h.data.empty()) continue;
    const uint8_t* data = h.data.data();
    const size_t size = h.data.size();

    std::vector<size_t> seps;
    for (size_t i = 0; i + 3 < size; i += 2) {
      if (u16(data + i) == 0x7FFF && u16(data + i + 2) == 0x7FFF) seps.push_back(i);
    }
    for (size_t i = 0; i < seps.size(); ++i) {
      size_t start = seps[i] + 4;
      size_t end = (i + 1 < seps.size()) ? seps[i + 1] : size;
      parse_record(meshes, data, size, start, end, int(hi));
    }
  }
  return meshes;
}

void render_frame(std::vector<uint32_t>& fb, int w, int h, const std::vector<Mesh>& meshes,
                  int& mesh_index, float& yaw, float& pitch, float& zoom,
                  DrawTextFn draw_text) {
  std::fill(fb.begin(), fb.end(), 0xFF101018u);
  if (meshes.empty()) {
    if (draw_text)
      draw_text(fb, w, h, 16, 16, "No aircraft meshes found in executable", 2, 0xFFFF6060u);
    return;
  }
  if (mesh_index < 0) mesh_index = 0;
  if (mesh_index >= int(meshes.size())) mesh_index = int(meshes.size()) - 1;
  const Mesh& m = meshes[size_t(mesh_index)];

  const float cy = std::cos(yaw), sy = std::sin(yaw);
  const float cp = std::cos(pitch), sp = std::sin(pitch);
  const float scale = zoom * float(std::min(w, h)) / 1400.f;
  const float cx = float(w) * 0.5f, cy_s = float(h) * 0.55f;

  auto project = [&](int16_t x, int16_t y, int16_t z, int& ox, int& oy) {
    // yaw around Y, pitch around X; Y is up
    float fx = float(x), fy = float(y), fz = float(z);
    float x1 = fx * cy + fz * sy;
    float z1 = -fx * sy + fz * cy;
    float y2 = fy * cp - z1 * sp;
    float z2 = fy * sp + z1 * cp;
    ox = int(cx + x1 * scale);
    oy = int(cy_s - y2 * scale);
    (void)z2;
  };

  // Ground crosshair
  draw_line(fb, w, h, int(cx) - 20, int(cy_s), int(cx) + 20, int(cy_s), 0xFF303040u);
  draw_line(fb, w, h, int(cx), int(cy_s) - 20, int(cx), int(cy_s) + 20, 0xFF303040u);

  std::vector<int> px(m.vx.size()), py(m.vx.size());
  for (size_t i = 0; i < m.vx.size(); ++i)
    project(m.vx[i], m.vy[i], m.vz[i], px[i], py[i]);

  const uint32_t col = 0xFF40F0FFu;
  for (auto [a, b] : m.edges)
    draw_line(fb, w, h, px[size_t(a)], py[size_t(a)], px[size_t(b)], py[size_t(b)], col);
  // verts as dots
  for (size_t i = 0; i < px.size(); ++i) {
    put_px(fb, w, h, px[i], py[i], 0xFFFFFFFFu);
    put_px(fb, w, h, px[i] + 1, py[i], 0xFFFFFFFFu);
    put_px(fb, w, h, px[i], py[i] + 1, 0xFFFFFFFFu);
  }

  if (!draw_text) return;
  const int sc = std::max(1, w / 320);
  char line[128];
  std::snprintf(line, sizeof(line), "MESH VIEWER  %d / %d", mesh_index + 1, int(meshes.size()));
  draw_text(fb, w, h, 8 * sc, 8 * sc, line, sc, 0xFFFFD040u);
  draw_text(fb, w, h, 8 * sc, 20 * sc, m.label, sc, 0xFFB0B0B0u);
  if (m.pp >= 0)
    std::snprintf(line, sizeof(line), "pp=$%02X  N=%d/%zu", m.pp, m.declared_n, m.vx.size());
  else
    std::snprintf(line, sizeof(line), "pp=—  N=%d/%zu", m.declared_n, m.vx.size());
  draw_text(fb, w, h, 8 * sc, 32 * sc, line, sc, 0xFF80A0FFu);
  draw_text(fb, w, h, 8 * sc, h - 28 * sc, "Left/Right mesh  Arrows/stick orbit  +/- zoom", sc,
            0xFF808090u);
  draw_text(fb, w, h, 8 * sc, h - 16 * sc, "B/Back/Esc close", sc, 0xFF808090u);
}

}  // namespace mesh_viewer
