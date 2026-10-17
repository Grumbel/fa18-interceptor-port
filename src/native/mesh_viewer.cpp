#include "mesh_viewer.hpp"

#include "amiga/hunk.hpp"

#include <algorithm>
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

void consume_edges(Mesh& m, const uint8_t* data, size_t size, size_t at) {
  size_t p = at;
  const int nverts = int(m.vx.size());
  while (p + 2 <= size) {
    uint16_t cmd = u16(data + p);
    p += 2;
    if (cmd == 0x800C || cmd == 0xC00C) {
      std::vector<int> idxs;
      while (p + 2 <= size) {
        uint16_t w = u16(data + p);
        if (is_opcode(w) || w == 0xFFFF) break;
        int vi = int(w) / 6;
        if (vi >= 0 && vi < nverts) idxs.push_back(vi);
        p += 2;
      }
      for (size_t i = 0; i + 1 < idxs.size(); ++i)
        add_edge(m, idxs[i], idxs[i + 1]);
      if (idxs.size() >= 3) add_edge(m, idxs.back(), idxs.front());
      if (p + 2 <= size) {
        uint16_t endc = u16(data + p);
        if (endc == 0x8018 || endc == 0x801E || endc == 0x8024 || endc == 0x802A ||
            endc == 0x8000) {
          p += 2;
          if (endc != 0x8000 && p + 2 <= size) p += 2;  // attr
        }
      }
      continue;
    }
    break;
  }
}

Mesh* parse_record(std::vector<Mesh>& out, const uint8_t* data, size_t size,
                   size_t start, size_t end, int hunk_i) {
  if (end < start + 16) return nullptr;
  uint16_t t0 = u16(data + start), t1 = u16(data + start + 2);
  if (t0 != 0x0100) return nullptr;

  size_t off = start + 4;
  while (off + 2 <= end && u16(data + off) == 0) off += 2;

  // hull until FFFF FFFF
  while (off + 4 <= end) {
    uint16_t a = u16(data + off), b = u16(data + off + 2);
    if (a == 0xFFFF && b == 0xFFFF) {
      off += 4;
      break;
    }
    if (is_opcode(a)) return nullptr;
    off += 4;
  }
  if (off + 2 > end) return nullptr;

  Mesh m;
  m.tags0 = t0;
  m.tags1 = t1;
  m.declared_n = u16(data + off);
  off += 2;

  while (off + 6 <= end) {
    uint16_t w = u16(data + off);
    if (is_opcode(w)) break;
    int16_t x = s16(data + off), y = s16(data + off + 2), z = s16(data + off + 4);
    if (std::max({std::abs(int(x)), std::abs(int(y)), std::abs(int(z))}) > 4096) break;
    m.vx.push_back(x);
    m.vy.push_back(y);
    m.vz.push_back(z);
    off += 6;
  }
  if (m.vx.size() < 3) return nullptr;

  size_t dl = off;
  if (off + 4 <= end && u16(data + off) == 0x4070) {
    uint16_t nxt = u16(data + off + 2);
    if ((nxt & 0xFF00) == 0x4000) m.pp = int(nxt & 0xFF);
  }

  std::vector<int> offsets;
  size_t scan = off;
  while (scan + 2 <= end) {
    uint16_t w = u16(data + scan);
    if ((w & 0xF000) == 0x8000 && (w & 0x0FFF) >= 0x10 && (w & 0x0FFF) <= 0x200) {
      scan += 2;
      if (scan + 2 <= end && (u16(data + scan) & 0xF000) == 0xA000) scan += 2;
      while (scan + 2 <= end) {
        uint16_t o = u16(data + scan);
        if (o == 0 || o >= 0x4000) break;
        offsets.push_back(int(o));
        scan += 2;
      }
      break;
    }
    scan += 2;
  }

  for (int o : offsets) {
    size_t abs = dl + size_t(o);
    if (abs < size) consume_edges(m, data, size, abs);
  }
  for (size_t p = dl; p + 2 < end; p += 2) {
    if (u16(data + p) == 0x800C) consume_edges(m, data, size, p);
  }

  char buf[64];
  std::snprintf(buf, sizeof(buf), "H%d +$%zx  %zu verts  %zu edges", hunk_i, start,
                m.vx.size(), m.edges.size());
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
