// Ground / sea model of the 3D view, decoded from the tables of $3f284
// (terrain tiles), $331f0/$33420 (zone tables) and $42f38 (sky quad).
// See docs/ground_sea_sky.md.
//
// The original paints, in this order: a land backdrop over the whole view
// (palette index 4), the water polygons of the terrain tiles (plane 1 set:
// index 6) and the "sky" quad, a ground-sized plane mirrored to the
// camera's height above it (plane 0 set: index 5).  All terrain polygons
// lie in the plane Y = 0, so in world space the scene is: a land plane,
// flat water polygons on it, and sky above the horizon.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace terrain {

// One flat water polygon in world coordinates (X, Z; Y = 0).
struct Polygon {
  std::vector<std::pair<int64_t, int64_t>> v;
  int32_t min_altitude = 0;  // drawn only when the scaled altitude >= this
};

struct Tile {
  std::vector<Polygon> polys;        // the full mesh
  // Simplified mesh: used for near-grid tiles farther than an altitude
  // dependent distance at low altitude, and for the camera's own tile when
  // $5ab94 is set ($3f6a4).
  std::vector<Polygon> coarse;
  uint32_t record = 0;               // runtime address of the tile record
};

enum Grid { Near = 0, Far = 1 };

struct Terrain {
  // Near grid: 32 x 32 tiles of one sector (2^24 world units), vertices in
  // units of 4096.  Far grid: 8 x 8 tiles of 2^28 world units (tile 4 starts
  // at X = 0), vertices in units of 65536.  Index [row = Z][col = X].
  Tile near[32][32];
  Tile far[8][8];
};

using Reader = std::function<uint8_t(uint32_t)>;

bool decode(const Reader& rd, Terrain& t);

// World X of the origin of a tile, and the unit of its vertex coordinates.
inline int64_t tile_origin(Grid g, int index) {
  return g == Near ? int64_t(index) << 24 : int64_t(index - 4) << 28;
}
inline int64_t vertex_unit(Grid g) { return g == Near ? 4096 : 65536; }

// The scaled altitude the LOD thresholds compare with (fp-40 of $3f284):
// alt = -$5ad70; unless $5aad5 or alt > 524272: alt = (alt >> 4) *
// (32768 / max($5ad3a, 2)) >> 4.
int32_t scaled_altitude(int32_t alt_5ad70, uint8_t flag_5aad5, int16_t scale_5ad3a);

}  // namespace terrain

namespace world { struct World; }

namespace terrain {
// Top-down map of the world (X right, Z down): land, the water polygons of
// the near grid and the static objects of the near map.  For checking the
// decoders (`fa18 --map-dump FILE.ppm`).
bool write_map(const Terrain& t, const world::World* w, const std::string& path, int size = 1024);
}  // namespace terrain
