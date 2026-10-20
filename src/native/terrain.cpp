// Terrain (water polygon) tables of $3f284.  See terrain.hpp.
#include "native/terrain.hpp"

namespace terrain {

namespace {

constexpr uint32_t kNearGrid = 0x58168;  // 32 x 32 words, row stride 64
constexpr uint32_t kFarGrid = 0x57fa4;   // 8 x 8 words, row stride 16
constexpr int kMaxVerts = 18;            // $3f74e: more is error 32

struct Mem {
  const Reader& rd;
  uint16_t w(uint32_t a) const { return uint16_t(rd(a) << 8 | rd(a + 1)); }
  int16_t sw(uint32_t a) const { return int16_t(w(a)); }
  int32_t sl(uint32_t a) const { return int32_t(uint32_t(w(a)) << 16 | w(a + 2)); }
};

// Polygon list ($3f72e): n > 0: polygon of n (x, z) words; other negative
// words except -1: the following polygons need altitude >= (w & $7fff) * 4;
// -1: end.
void read_list(const Mem& m, uint32_t a, Grid g, int col, int row, std::vector<Polygon>& out) {
  int32_t min_alt = 0;
  const int64_t ox = tile_origin(g, col), oz = tile_origin(g, row), unit = vertex_unit(g);
  for (int guard = 0; guard < 256; ++guard) {
    int16_t n = m.sw(a);
    a += 2;
    if (n <= 0) {
      if (n == -1) return;
      min_alt = int32_t(uint16_t(n) & 0x7FFF) * 4;
      n = m.sw(a);
      a += 2;
    }
    if (n <= 0 || n > kMaxVerts) return;  // the original stops the tile here
    Polygon p;
    p.min_altitude = min_alt;
    for (int k = 0; k < n; ++k, a += 4)
      p.v.push_back({ox + m.sw(a) * unit, oz + m.sw(a + 2) * unit});
    out.push_back(std::move(p));
  }
}

void read_tile(const Mem& m, uint32_t grid, uint32_t cell, Grid g, int col, int row, Tile& t) {
  int16_t off = m.sw(cell);
  if (off <= 0) return;  // error 64 in the original
  uint32_t rec = grid + uint32_t(off);
  t.record = rec;
  int32_t coarse = m.sl(rec);
  if (coarse < 0) return;  // no water in this tile
  read_list(m, rec + 4, g, col, row, t.polys);
  if (coarse > 0) read_list(m, uint32_t(coarse), g, col, row, t.coarse);
}

}  // namespace

bool decode(const Reader& rd, Terrain& t) {
  Mem m{rd};
  for (int r = 0; r < 32; ++r)
    for (int c = 0; c < 32; ++c)
      read_tile(m, kNearGrid, kNearGrid + uint32_t(r * 64 + c * 2), Near, c, r, t.near[r][c]);
  for (int r = 0; r < 8; ++r)
    for (int c = 0; c < 8; ++c)
      read_tile(m, kFarGrid, kFarGrid + uint32_t(r * 16 + c * 2), Far, c, r, t.far[r][c]);
  return true;
}

int32_t scaled_altitude(int32_t alt_5ad70, uint8_t flag_5aad5, int16_t scale_5ad3a) {
  int32_t alt = -alt_5ad70;
  if (flag_5aad5 || alt > 524272) return alt;
  alt >>= 4;
  uint32_t k = 32768u / uint32_t(scale_5ad3a < 2 ? 2 : uint16_t(scale_5ad3a));
  return int32_t((uint32_t(uint16_t(alt)) * uint16_t(k)) >> 4);
}

}  // namespace terrain

// ---- top-down map --------------------------------------------------------------------
#include "native/world.hpp"

#include <algorithm>
#include <cstdio>

namespace terrain {

namespace {
void fill_polygon(std::vector<uint32_t>& img, int size, const std::vector<std::pair<double, double>>& v,
                  uint32_t colour) {
  if (v.size() < 3) return;
  double y0 = 1e30, y1 = -1e30;
  for (auto& p : v) {
    y0 = std::min(y0, p.second);
    y1 = std::max(y1, p.second);
  }
  for (int y = std::max(0, int(y0)); y <= std::min(size - 1, int(y1)); ++y) {
    double sy = y + 0.5;
    std::vector<double> xs;
    for (size_t i = 0; i < v.size(); ++i) {
      auto a = v[i], b = v[(i + 1) % v.size()];
      if ((a.second <= sy) == (b.second <= sy)) continue;
      xs.push_back(a.first + (sy - a.second) * (b.first - a.first) / (b.second - a.second));
    }
    std::sort(xs.begin(), xs.end());
    for (size_t i = 0; i + 1 < xs.size(); i += 2)
      for (int x = std::max(0, int(xs[i] + 0.5)); x < std::min(size, int(xs[i + 1] + 0.5)); ++x)
        img[size_t(y) * size_t(size) + size_t(x)] = colour;
  }
}
}  // namespace

bool write_map(const Terrain& t, const world::World* w, const std::string& path, int size) {
  std::vector<uint32_t> img(size_t(size) * size_t(size), 0x115511);
  const double world = double(1 << 29), k = size / world;
  for (int r = 0; r < 32; ++r)
    for (int c = 0; c < 32; ++c)
      for (const Polygon& p : t.near[r][c].polys) {
        // Polygons gated by altitude in a lighter blue.
        std::vector<std::pair<double, double>> v;
        for (auto& [x, z] : p.v) v.push_back({double(x) * k, double(z) * k});
        fill_polygon(img, size, v, p.min_altitude > 0 ? 0x3080c0 : 0x003366);
      }
  // sector grid
  for (int i = 0; i < size; ++i)
    for (int s = 0; s < 32; ++s) {
      int g = s * size / 32;
      img[size_t(i) * size_t(size) + size_t(g)] ^= 0x101010;
      img[size_t(g) * size_t(size) + size_t(i)] ^= 0x101010;
    }
  if (w) {
    static const uint32_t col[world::kMaps] = {0xFFFF60, 0xFF6060, 0xFFFFFF};
    for (int m = 0; m < world::kMaps; ++m)
      for (const world::Cell& cell : w->maps[m].cells)
        for (const world::Object& o : cell.objects) {
          int32_t X, Z;
          world::object_units(world::MapId(m), cell, o, X, Z);
          int px = int(double(X) * 256 * k), pz = int(double(Z) * 256 * k);
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
              if (px + dx >= 0 && pz + dy >= 0 && px + dx < size && pz + dy < size)
                img[size_t(pz + dy) * size_t(size) + size_t(px + dx)] = col[m];
        }
  }
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n%d %d\n255\n", size, size);
  for (uint32_t c : img) {
    uint8_t rgb[3] = {uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c)};
    std::fwrite(rgb, 1, 3, f);
  }
  std::fclose(f);
  return true;
}

}  // namespace terrain
