#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include <iostream>
#include "transform.hpp"
#include "draw.hpp"
#include "gfx_state.hpp"

namespace fa18 {

// Model format recovered from in-flight dump cluster $00C39700–$00C3A600.
// Pointer table $00C4C4C8 (abs $5400) references these headers.
//
//   [2D hull word-pairs…]  FFFF FFFF
//   word N                 — declared vertex count (often under-states)
//   (x,y,z).w × N+         — signed local coords; more verts follow until
//                            a display-list opcode (word with bits 14–15 set
//                            and |value| large, typically $40xx/$80xx/$C0xx)
//   display list…          — edge/poly commands (encoding still open)
//
// Scale: raw / kMeshScale → world units for the port.

constexpr float kMeshScale = 64.f;

struct Mesh {
  std::vector<Vec3> verts;
  std::vector<std::pair<int, int>> edges;
  const char* name = "";
};

inline Vec3 V(int16_t x, int16_t y, int16_t z) {
  return {x / kMeshScale, y / kMeshScale, z / kMeshScale};
}

// ---------------------------------------------------------------------------
// Aircraft-like mesh from $00C39738
// 8-fold symmetric XZ cross + vertical tip — typical of the cluster.
// ---------------------------------------------------------------------------
inline Mesh mesh_aircraft_39738() {
  Mesh m;
  m.name = "aircraft_$00C39738";
  // Declared count=5 but stream continues to 10 usable verts
  m.verts = {
    V(  40,   0,  -40),  // 0 body
    V(   0,   0, -500),  // 1 nose
    V( -40,   0,  -40),  // 2 body
    V(-500,   0,    0),  // 3 left wing
    V( -40,   0,   40),  // 4 body
    V(   0,   0,  500),  // 5 tail
    V(  40,   0,   40),  // 6 body
    V( 500,   0,    0),  // 7 right wing
    V(  40, 400,  -40),  // 8 vertical / canopy
    V(   0, 100,    0),  // 9 center-up
  };
  m.edges = {
    // fuselage nose → tail
    {1, 0}, {0, 9}, {9, 5}, {1, 2}, {2, 9}, {9, 4}, {4, 5},
    // wings
    {3, 0}, {3, 2}, {3, 9},
    {7, 0}, {7, 6}, {7, 9},
    // body ring
    {0, 2}, {2, 4}, {4, 6}, {6, 0},
    // vertical
    {8, 0}, {8, 9}, {8, 1},
  };
  return m;
}

// ---------------------------------------------------------------------------
// Variant at $00C39800 (slightly different proportions)
// ---------------------------------------------------------------------------
inline Mesh mesh_aircraft_39800() {
  Mesh m;
  m.name = "aircraft_$00C39800";
  m.verts = {
    V( 100,   0, -120),
    V(   0,   0, -437),
    V(-100,   0, -120),
    V(-525,   0,    0),
    V(-100,   0,  120),
    V(   0,   0,  437),
    V( 100,   0,  120),
    V( 525,   0,    0),
    V( 100, 140, -120),
    V(   0, 750,    0),
  };
  m.edges = {
    {1, 0}, {0, 9}, {9, 5}, {1, 2}, {2, 9}, {9, 4}, {4, 5},
    {3, 0}, {3, 2}, {3, 9},
    {7, 0}, {7, 6}, {7, 9},
    {0, 2}, {2, 4}, {4, 6}, {6, 0},
    {8, 0}, {8, 9}, {8, 1},
  };
  return m;
}

// ---------------------------------------------------------------------------
// Mesh B (table slot) $00C39B52/66 — same family, scale -320
// ---------------------------------------------------------------------------
inline Mesh mesh_from_dump_B() {
  Mesh m;
  m.name = "dump_B_$00C39B66";
  m.verts = {
    V(  40,   0,  -40),
    V(   0,   0, -320),
    V( -40,   0,  -40),
    V(-320,   0,    0),
    V( -40,   0,   40),
    V(   0,   0,  320),
    V(  40,   0,   40),
    V( 320,   0,    0),
    V(  40, 256,  -40),
    V(   0, 100,    0),
  };
  m.edges = {
    {1, 0}, {0, 9}, {9, 5}, {1, 2}, {2, 9}, {9, 4}, {4, 5},
    {3, 0}, {3, 2}, {3, 9},
    {7, 0}, {7, 6}, {7, 9},
    {0, 2}, {2, 4}, {4, 6}, {6, 0},
    {8, 0}, {8, 9},
  };
  return m;
}

// ---------------------------------------------------------------------------
// Mesh A $00C3A58C/A5A0 — larger extents (terrain / structure?)
// ---------------------------------------------------------------------------
inline Mesh mesh_from_dump_A() {
  Mesh m;
  m.name = "dump_A_$00C3A5A0";
  m.verts = {
    V( 2112,    0,  -896),
    V( 1152, 6764, -4192),
    V(-2080,    0, -2048),
    V(-3328, 3720,   480),
    V(-1600,    0,  1280),
    V( -288, 3630,  2688),
    V( 1088,    0,  1568),
    V( 2464, 5726,  1024),
  };
  // Sequential ring + spokes
  for (int i = 0; i < 8; ++i)
    m.edges.push_back({i, (i + 1) % 8});
  for (int i = 1; i < 8; ++i)
    m.edges.push_back({0, i});
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
