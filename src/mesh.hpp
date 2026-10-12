#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include "transform.hpp"
#include "draw.hpp"
#include "gfx_state.hpp"

namespace fa18 {

// Model format recovered from in-flight dump at $00C39B52 / $00C3A58C
// (pointer table $00C4C4C8 / abs $5400):
//
//   word pairs …  FFFF FFFF   — 2D hull / radar silhouette (optional)
//   word count                — number of 3D vertices
//   count × (x,y,z) words     — signed 16-bit local coords
//   remaining data            — faces/edges (still partially open)
//
// Scale: raw units are large; port divides by kMeshScale for world space.

constexpr float kMeshScale = 64.f;

struct Mesh {
  std::vector<Vec3> verts;                 // local space
  std::vector<std::pair<int, int>> edges;  // indices into verts
  const char* name = "";
};

// Build sequential edges + close the loop.
inline void edges_loop(Mesh& m) {
  const int n = static_cast<int>(m.verts.size());
  for (int i = 0; i + 1 < n; ++i)
    m.edges.push_back({i, i + 1});
  if (n >= 3)
    m.edges.push_back({n - 1, 0});
}

// Connect every vertex to the next and to vertex 0 (fan) — useful when
// the true edge list is unknown.
inline void edges_fan(Mesh& m) {
  const int n = static_cast<int>(m.verts.size());
  for (int i = 1; i < n; ++i)
    m.edges.push_back({0, i});
  for (int i = 1; i + 1 < n; ++i)
    m.edges.push_back({i, i + 1});
}

// ---------------------------------------------------------------------------
// Mesh B — live $00C39B52 (5 verts).  Likely a simple craft / debris shape.
// ---------------------------------------------------------------------------
inline Mesh mesh_from_dump_B() {
  Mesh m;
  m.name = "dump_B_$00C39B52";
  // xyz as extracted after FFFF FFFF / count=5
  const int16_t raw[][3] = {
    {  40, 0,  -40},
    {   0, 0, -320},
    { -40, 0,  -40},
    {-320, 0,    0},
    { -40, 0,   40},
    // continuation candidates from following words (best-effort)
    {   0, 0,  320},
    {  40, 0,   40},
    { 320, 0,    0},
  };
  for (auto& r : raw)
    m.verts.push_back({r[0] / kMeshScale, r[1] / kMeshScale, r[2] / kMeshScale});
  // Hand-authored edges for a crude aircraft-like silhouette in XZ
  m.edges = {
    {1, 0}, {0, 2}, {2, 1},          // rear triangle
    {3, 0}, {3, 2},                   // left wing
    {5, 0}, {5, 6}, {6, 7}, {7, 5},  // right side
    {1, 3}, {1, 5},                   // fuselage spans
  };
  return m;
}

// ---------------------------------------------------------------------------
// Mesh A — live $00C3A58C (4 verts).  Larger extents.
// ---------------------------------------------------------------------------
inline Mesh mesh_from_dump_A() {
  Mesh m;
  m.name = "dump_A_$00C3A58C";
  const int16_t raw[][3] = {
    { 2112,    0,  -896},
    { 1152, 6764, -4192},
    {-2080,    0, -2048},
    {-3328, 3720,   480},
  };
  for (auto& r : raw)
    m.verts.push_back({r[0] / kMeshScale, r[1] / kMeshScale, r[2] / kMeshScale});
  edges_loop(m);
  return m;
}

// Draw mesh in world space with orientation + position.
inline void draw_mesh(SDL_Renderer* ren, GfxState& gfx, const Mesh& mesh,
                      const Mat3Fixed& orient, const Vec3& pos, int pen) {
  gfx.pen = static_cast<uint16_t>(pen);
  std::vector<int> sx(mesh.verts.size()), sy(mesh.verts.size());
  std::vector<bool> ok(mesh.verts.size(), false);
  for (size_t i = 0; i < mesh.verts.size(); ++i) {
    const auto& lv = mesh.verts[i];
    int16_t lx = static_cast<int16_t>(lv.x * 64.f);
    int16_t ly = static_cast<int16_t>(lv.y * 64.f);
    int16_t lz = static_cast<int16_t>(lv.z * 64.f);
    Vec3 w = transform_point_fp(orient, lx, ly, lz);
    w.x += pos.x; w.y += pos.y; w.z += pos.z;
    w = apply_view(w, gfx.view_x, gfx.view_y, gfx.view_z);
    if (w.z > 0.2f) {
      project(w, sx[i], sy[i]);
      ok[i] = true;
    }
  }
  for (auto [a, b] : mesh.edges) {
    if (a < 0 || b < 0 ||
        a >= static_cast<int>(ok.size()) ||
        b >= static_cast<int>(ok.size()))
      continue;
    if (!ok[a] || !ok[b]) continue;
    blitter_line_stub(ren, gfx, sx[a], sy[a], sx[b], sy[b]);
  }
}

}  // namespace fa18
