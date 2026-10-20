// Offline 3D model viewer (debug menu).
//
// Decodes the object models the way the game draws them: every entry of the
// object type table $36094 (draw routine + model base + LOD list), the LOD
// list and mesh header walked like render_body ($31d6c) / distant_body
// ($32d7e), the vertex block and its extra vertex runs, the BSP/group
// stream, and the polygon lists with the original op handlers (ops_body.inc,
// run on a private copy of the executable).  The rasteriser calls of the ops
// are captured as polygons, lines and points in object space.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mesh_viewer {

struct Vec3 {
  int32_t x = 0, y = 0, z = 0;
};

struct Prim {
  enum Kind : uint8_t { Poly, Line, Point } kind = Poly;
  // Bitplane operation: planes in `mask` ($5a9df) are set to `colour`,
  // planes in `xr` are inverted; the others keep the background.
  uint8_t colour = 0, mask = 15, xr = 0;
  uint8_t index(uint8_t background) const {
    uint8_t set_planes = uint8_t(mask & ~xr);
    return uint8_t((((background & ~set_planes) | colour) ^ xr) & 15);
  }
  std::vector<Vec3> v;
};

struct Lod {
  int distance = 0;       // LOD distance the level was decoded for
  uint32_t mesh = 0;      // mesh header ($5ad2a)
  uint32_t stream = 0;    // draw stream ($5ad2e)
  uint8_t flags = 0;      // header byte +7
  int base_verts = 0;     // vertex count of the header
  std::vector<Prim> prims;
  std::vector<std::string> notes;  // ops/paths that could not be decoded
};

struct Model {
  std::vector<int> types;  // type table indices sharing this model
  uint32_t draw_fn = 0, base = 0, lod_list = 0;
  bool distant = false;    // $32d64/$32d72 ground-object variant
  std::vector<Lod> lods;   // nearest first
};

// Decode all models of the type table from the hunk executable.
std::vector<Model> load_models(const std::vector<uint8_t>& exe);

using DrawTextFn = void (*)(std::vector<uint32_t>&, int, int, int, int,
                            const std::string&, int, uint32_t);

struct ViewState {
  int model = 0, lod = 0;
  float yaw = 0.7f, pitch = 0.45f, zoom = 1.0f;
  bool wire = false;           // outline polygons
  bool live_palette = false;   // machine palette instead of the flight palette
};

// Render one frame of the viewer into `fb` (w*h ARGB).  `live_pal` holds the
// machine's COLOR00..15 (12-bit), used when state.live_palette is set.
void render_frame(std::vector<uint32_t>& fb, int w, int h, const std::vector<Model>& models,
                  ViewState& state, const uint16_t live_pal[16], DrawTextFn draw_text);

// Headless: decode, print a summary and write one PPM per model LOD into dir.
bool dump_models(const std::vector<uint8_t>& exe, const std::string& dir);

}  // namespace mesh_viewer
