#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include <iostream>
#include <cstring>
#include "transform.hpp"
#include "draw.hpp"
#include "gfx_state.hpp"

namespace fa18 {

// Model format recovered from in-flight dump cluster $00C39700–$00C3A600.
// Pointer table $00C4C4C8 (abs $5400) references **2-D hull starts**
// (not the FFFF/N vertex header).
//
// Full cluster record:
//   7FFF 7FFF              — record separator
//   tag0 tag1              — type tags (aircraft: 0100 0200)
//   0000 × N               — reserved / padding
//   (x,y).w pairs…         — 2-D top-down hull  ← $5400 points here
//   FFFF FFFF              — end of hull
//   word N                 — declared vertex count (often under-states)
//   (x,y,z).w × N+         — signed local coords; continues past N
//   display list…          — edge/poly commands
//   7FFF 7FFF              — end of record / next separator
//
// Opcode heuristic (word w):
//   0x4000–0x7FFF  → command (bit 14 set)
//   0x8000–0xDFFF  → command (bit 15 set, not a small negative coord)
//   0xE000–0xFFFF  → small negative coordinate (|v| < 8192)
//
// Scale: raw / kMeshScale → world units for the port.

constexpr float kMeshScale = 64.f;

struct Mesh {
  std::vector<Vec3> verts;
  std::vector<std::pair<int, int>> edges;
  // Parallel to edges when known: Amiga palette index from attr hi/4.
  // Empty → draw_mesh uses the caller-supplied default pen.
  std::vector<uint8_t> edge_pens;
  std::vector<std::pair<int16_t, int16_t>> hull;  // 2-D silhouette
  std::vector<uint16_t> display_list;  // raw opcodes
  uint16_t tag0 = 0;
  uint16_t tag1 = 0;
  const char* name = "";
};

inline Vec3 V(int16_t x, int16_t y, int16_t z) {
  return {x / kMeshScale, y / kMeshScale, z / kMeshScale};
}

inline int16_t s16(uint16_t u) {
  return static_cast<int16_t>(u);
}

// True if the word is a display-list opcode rather than a coordinate.
inline bool is_mesh_opcode(uint16_t w) {
  if (w >= 0x4000 && w < 0x8000) return true;   // 4xxx
  if (w >= 0x8000 && w < 0xE000) return true;   // 8xxx / Cxxx (not E/F)
  return false;
}

// Parse a mesh record from a big-endian word stream.
// Accepts either a full cluster record (starting at 7FFF 7FFF) or a
// mid-record pointer at the FFFF hull terminator / N field.
inline Mesh mesh_parse(const uint16_t* words, size_t count, const char* name) {
  Mesh m;
  m.name = name;
  size_t i = 0;

  // Full record: 7FFF 7FFF tag0 tag1 …
  if (i + 3 < count && words[i] == 0x7FFF && words[i + 1] == 0x7FFF) {
    i += 2;
    m.tag0 = words[i++];
    m.tag1 = words[i++];
    while (i < count && words[i] == 0)
      ++i;
  }

  // 2-D hull pairs up to FFFF FFFF
  while (i + 1 < count) {
    if (words[i] == 0xFFFF && words[i + 1] == 0xFFFF) {
      i += 2;
      break;
    }
    // Skip leading 7FFF 7FFF if we started mid-stream
    if (words[i] == 0x7FFF && words[i + 1] == 0x7FFF) {
      i += 2;
      continue;
    }
    m.hull.emplace_back(s16(words[i]), s16(words[i + 1]));
    i += 2;
    if (m.hull.size() > 64) break;
  }
  if (i >= count) return m;

  uint16_t N = words[i++];
  (void)N;  // declared count is only a lower bound
  while (i + 2 < count) {
    if (is_mesh_opcode(words[i]) ||
        is_mesh_opcode(words[i + 1]) ||
        is_mesh_opcode(words[i + 2]))
      break;
    m.verts.push_back(V(s16(words[i]), s16(words[i + 1]), s16(words[i + 2])));
    i += 3;
    if (m.verts.size() > 64) break;
  }
  // Display-list until 7FFF end marker or limit
  while (i < count && m.display_list.size() < 128) {
    uint16_t w = words[i++];
    m.display_list.push_back(w);
    if (w == 0x7FFF && m.display_list.size() > 4)
      break;
    if (w == 0xFFFF && m.display_list.size() > 4)
      break;
  }
  return m;
}

// ---------------------------------------------------------------------------
// Partial display-list → edge decoder
//
// Two-level structure observed in aircraft models:
//
//   Top level:  40xx / C016 state commands, then a short offset table
//               (words in ~$0100–$07FF).  Offsets are **byte offsets from
//               the display-list start** (first opcode word).
//
//   Sub-lists at those offsets:
//     8xxx / Cxxx command word
//       $800C  begin edge/poly
//       $8000  end of edge group
//       $8018/$8024/$802A/$801E  edge group with flags
//       $C00C  polyline variant
//     zero or more vertex indices as **byte offsets**
//       (index × 6, since each vertex is 3 × int16)
//     optional trailing params / FFFF
//
//   Example:  800C 0006 0024 002A  →  cmd, verts 1, 6, 7
//
// Top-level 40xx low-byte params remain open.
// ---------------------------------------------------------------------------
inline std::vector<std::pair<int, int>>
mesh_edges_from_dl(const uint16_t* mesh_words, size_t mesh_word_count,
                   size_t nverts, uint32_t /*mesh_base_addr*/ = 0,
                   std::vector<uint8_t>* out_pens = nullptr) {
  std::vector<std::pair<int, int>> edges;
  if (out_pens) out_pens->clear();
  if (!mesh_words || mesh_word_count < 4 || nverts == 0)
    return edges;

  // Locate DL start (first opcode after hull / N / verts)
  size_t i = 0;
  while (i + 1 < mesh_word_count) {
    if (mesh_words[i] == 0xFFFF && mesh_words[i + 1] == 0xFFFF) {
      i += 2;
      break;
    }
    ++i;
  }
  if (i >= mesh_word_count) return edges;
  ++i;  // skip N
  while (i + 2 < mesh_word_count) {
    if (is_mesh_opcode(mesh_words[i]) ||
        is_mesh_opcode(mesh_words[i + 1]) ||
        is_mesh_opcode(mesh_words[i + 2]))
      break;
    i += 3;
  }
  const size_t dl_start = i;  // word index of first DL opcode

  // Collect offset-table entries.  Preferred pattern:
  //   Axxx  80LL  Axxx  [byte offsets from DL start…]
  // Offsets may be small ($3A) or large ($035C); stop at next opcode.
  std::vector<uint16_t> offsets;
  for (size_t j = dl_start; j + 3 < mesh_word_count && j < dl_start + 40; ++j) {
    const uint16_t a = mesh_words[j], b = mesh_words[j + 1], c = mesh_words[j + 2];
    if ((a & 0xF000) == 0xA000 && (b & 0xF000) == 0x8000 &&
        ((c & 0xF000) == 0xA000 || (c > 0x20 && c < 0x800))) {
      size_t k = ((c & 0xF000) == 0xA000) ? j + 3 : j + 2;
      while (k < mesh_word_count) {
        uint16_t v = mesh_words[k];
        // Accept pure data offsets; reject opcodes and terminators
        if (v == 0xFFFF || v == 0x7FFF) break;
        if ((v & 0xF000) >= 0x4000) break;  // 4xxx/8xxx/Axxx/Cxxx
        if (v < 0x10) break;                // too small to be useful
        offsets.push_back(v);
        ++k;
        if (offsets.size() > 10) break;
      }
      break;
    }
  }
  // Fallback: consecutive mid-range values (legacy heuristic)
  if (offsets.empty()) {
    for (size_t j = dl_start; j + 2 < mesh_word_count && j < dl_start + 80; ++j) {
      if (mesh_words[j] >= 0x100 && mesh_words[j] < 0x800 &&
          mesh_words[j + 1] >= 0x80 && mesh_words[j + 1] < 0x800 &&
          mesh_words[j + 2] >= 0x80 && mesh_words[j + 2] < 0x800) {
        size_t k = j;
        while (k < mesh_word_count &&
               mesh_words[k] >= 0x80 && mesh_words[k] < 0x800)
          offsets.push_back(mesh_words[k++]);
        break;
      }
    }
  }

  auto walk_sublist = [&](size_t sub) {
    size_t k = sub;
    while (k < mesh_word_count && k < sub + 40) {
      uint16_t w = mesh_words[k];
      if (w == 0xFFFF) break;
      const uint16_t hi = w & 0xF000;
      const uint16_t lo = w & 0xFF;
      if (hi == 0x8000 || hi == 0xC000) {
        ++k;
        // $800C / $C00C = begin: collect vertex byte-offsets
        // $8000 = end (no attrs); $8018/$1E/$24/$2A = end + attr word(s)
        std::vector<int> idxs;
        while (k < mesh_word_count) {
          uint16_t v = mesh_words[k];
          if (v < 0x200 && (v % 6) == 0) {
            int idx = v / 6;
            if (idx >= 0 && static_cast<size_t>(idx) < nverts)
              idxs.push_back(idx);
            ++k;
          } else {
            break;
          }
        }
        // End-with-attrs: attr hi/4 = colour candidate (0..7); lo = style.
        uint8_t pen = 0xFF;  // unknown
        if (lo != 0x00 && lo != 0x0C && k < mesh_word_count) {
          uint16_t attr = mesh_words[k];
          if (attr < 0x8000 && attr != 0xFFFF) {
            uint8_t hi = static_cast<uint8_t>(attr >> 8);
            if ((hi & 3) == 0 && hi / 4 <= 15)
              pen = static_cast<uint8_t>(hi / 4);
            ++k;
            if (k < mesh_word_count && mesh_words[k] < 0x100)
              ++k;
          }
        }
        auto emit = [&](int a, int b) {
          edges.emplace_back(a, b);
          if (out_pens) out_pens->push_back(pen);
        };
        for (size_t e = 1; e < idxs.size(); ++e)
          emit(idxs[e - 1], idxs[e]);
        if (idxs.size() >= 3)
          emit(idxs.back(), idxs.front());
      } else {
        ++k;
      }
    }
  };

  if (!offsets.empty()) {
    // Offsets are byte offsets from the DL start
    for (uint16_t off : offsets) {
      size_t sub = dl_start + off / 2;
      if (sub < mesh_word_count)
        walk_sublist(sub);
    }
  } else {
    // Fallback: scan whole DL for inline 8xxx edge commands
    walk_sublist(dl_start);
  }

  // Deduplicate (keep first pen for each undirected edge)
  std::vector<std::pair<int, int>> uniq;
  std::vector<uint8_t> uniq_pens;
  for (size_t i = 0; i < edges.size(); ++i) {
    int a = edges[i].first, b = edges[i].second;
    if (a == b) continue;
    bool found = false;
    for (auto [c, d] : uniq) {
      if ((a == c && b == d) || (a == d && b == c)) {
        found = true;
        break;
      }
    }
    if (!found) {
      uniq.emplace_back(a, b);
      if (out_pens && i < out_pens->size())
        uniq_pens.push_back((*out_pens)[i]);
      else if (out_pens)
        uniq_pens.push_back(0xFF);
    }
  }
  if (out_pens) *out_pens = std::move(uniq_pens);
  return uniq;
}

// ---------------------------------------------------------------------------
// Aircraft-like mesh from $00C39738
// 8-fold symmetric XZ cross + vertical tip — typical of the cluster.
// ---------------------------------------------------------------------------
inline Mesh mesh_aircraft_39738() {
  Mesh m;
  m.name = "aircraft_$00C39738";
  m.tag0 = 0x0100;
  m.tag1 = 0x0200;
  // Top-down 2-D hull (radar silhouette) from cluster record
  m.hull = {
    {800, 0}, {656, -296}, {296, -296}, {296, 656}, {-296, 296}, {-296, 296},
  };
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
  // Edges extracted from dump offset-table sub-lists (DL+$02D6..$035C).
  // Topology: nose/body/wing triangles; pens = attr_hi/4 (unconfirmed).
  m.edges = {
    {1, 2}, {2, 4}, {4, 1},
    {2, 6}, {6, 7}, {7, 2},
    {2, 3}, {3, 5}, {5, 2},
    {6, 3}, {5, 6},
    {3, 1}, {1, 0}, {0, 3},
    {0, 4}, {4, 7}, {7, 0},
  };
  m.edge_pens = {
    7, 7, 7,
    6, 6, 6,
    4, 4, 4,
    3, 3,
    2, 2, 2,
    1, 1, 1,
  };
  // Raw DL start: 4070 4022 C016 4240 0022 C016 4380 0022 …
  m.display_list = {
    0x4070, 0x4022, 0xC016, 0x4240, 0x0022, 0xC016, 0x4380, 0x0022,
    0xA244, 0x805C, 0xA23C, 0x0270, 0x02D6, 0x02E2, 0x0330, 0x035C,
  };
  return m;
}

// ---------------------------------------------------------------------------
// Variant at $00C39800 (slightly different proportions)
// ---------------------------------------------------------------------------
inline Mesh mesh_aircraft_39800() {
  Mesh m;
  m.name = "aircraft_$00C39800";
  m.tag0 = 0x0100;
  m.tag1 = 0x0200;
  m.hull = {
    {280, 0}, {396, -356}, {356, -376}, {376, 396}, {-356, 356}, {-376, 376},
  };
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
  // Dump-derived edges from offset-table sub-lists
  m.edges = {
    {2, 3}, {3, 5}, {5, 2},
    {1, 2}, {2, 4}, {4, 1},
    {3, 1}, {1, 0}, {0, 3},
  };
  m.edge_pens = {4, 4, 4, 7, 7, 7, 2, 2, 2};
  m.display_list = {
    0x4070, 0x4020, 0xC016, 0x4340, 0x0020, 0xC016, 0x4500, 0x0020,
    0xA17C, 0x805A, 0xA174, 0x01BA, 0x0226, 0x01EE, 0x0274,
  };
  return m;
}

// ---------------------------------------------------------------------------
// Smaller craft $00C398C6 (N=4 → 8 verts)
// ---------------------------------------------------------------------------
inline Mesh mesh_aircraft_398C6() {
  Mesh m;
  m.name = "aircraft_$00C398C6";
  m.verts = {
    V( 150,   0, -150),
    V(   0,   0, -234),
    V(-150,   0, -150),
    V(-234,   0,  234),
    V( 150,   0,  150),
    V( 234,   0,    0),
    V( 150,  50, -150),
    V(   0,1406,    0),
  };
  // Dump-derived edges from offset-table sub-lists
  m.edges = {
    {1, 6}, {6, 7}, {7, 1},
    {6, 3}, {3, 5}, {5, 6},
    {3, 1}, {1, 0}, {0, 3},
  };
  m.edge_pens = {4, 4, 4, 3, 3, 3, 2, 2, 2};
  m.display_list = {
    0x4070, 0x4020, 0xC016, 0x4340, 0x0020, 0xC016, 0x4500, 0x0020,
    0xA0C2, 0x805A, 0xA0BA, 0x00EE, 0x0112, 0x018E, 0x01BA,
  };
  return m;
}

// ---------------------------------------------------------------------------
// Mesh B (table slot) $00C39B66 — same family, scale ±320
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
  // Dump-derived edges from C00C sites at $00C39C3C+
  m.edges = {
    {1, 9}, {1, 2}, {2, 9}, {9, 3}, {3, 1},
  };
  m.edge_pens = {4, 4, 4, 4, 4};
  // DL: link-graph style (80xx cross-refs) rather than flat offset table
  m.display_list = {
    0x4380, 0x01C0, 0xA088, 0x81E8, 0xA080, 0x0006, 0x0000, 0x0000,
    0x0012, 0x0046, 0x0056, 0x0006, 0x0000, 0x0000, 0x0012, 0x005E,
  };
  return m;
}

// ---------------------------------------------------------------------------
// Banked / elevated variant $00C39E00 (Y ≠ 0 on body ring)
// ---------------------------------------------------------------------------
inline Mesh mesh_aircraft_39E00() {
  Mesh m;
  m.name = "aircraft_$00C39E00";
  m.verts = {
    V(  36, 352,   -9),
    V(   0,   0, -238),
    V( -36, 352,   -9),
    V( -59,   0,    0),
    V( -36, 352,    9),
    V(   0,   0,  238),
    V(  36, 352,    9),
    V(  59,   0,    0),
    V(  36, 564,   -9),
    V(   0,  20,    0),
  };
  // Dump-derived edges from offset-table sub-lists (DL+$3A..)
  m.edges = {
    {1, 8}, {8, 9}, {9, 1},
    {1, 2}, {2, 4}, {4, 1},
    {2, 6}, {6, 7}, {7, 2},
    {6, 3}, {3, 5}, {5, 6},
    {3, 1}, {1, 0}, {0, 3},
    {0, 4}, {4, 7}, {7, 0},
  };
  m.edge_pens = {
    7, 7, 7,
    6, 6, 6,
    3, 3, 3,
    2, 2, 2,
    1, 1, 1,
    1, 1, 1,
  };
  m.display_list = {
    0x40B0, 0x40BA, 0xC016, 0x4180, 0x00BA, 0xC016, 0x4480, 0x00BA,
    0xA022, 0x80F4, 0xA02A, 0x003A, 0x004C, 0x0058, 0x006C, 0x0080,
  };
  return m;
}

// ---------------------------------------------------------------------------
// Long-nose $00C39F66 (±2080)
// ---------------------------------------------------------------------------
inline Mesh mesh_aircraft_39F66() {
  Mesh m;
  m.name = "aircraft_$00C39F66";
  m.verts = {
    V( 104,   0,  -80),
    V(   0,   0,-2080),
    V(-104,   0,  -80),
    V(-1600,  0,    0),
    V(-104,   0,   80),
    V(   0,   0, 2080),
    V( 104,   0,   80),
    V( 1600,  0,    0),
    V( 104, 640,  -80),
    V(   0, 520,    0),
    V(   0,2020,   48),
  };
  m.edges = {
    {1, 0}, {0, 9}, {9, 5}, {1, 2}, {2, 9}, {9, 4}, {4, 5},
    {3, 0}, {3, 2}, {3, 9},
    {7, 0}, {7, 6}, {7, 9},
    {0, 2}, {2, 4}, {4, 6}, {6, 0},
    {8, 0}, {8, 9}, {8, 1},
    {10, 9},
  };
  return m;
}

// ---------------------------------------------------------------------------
// Mesh A $00C3A5A0 — larger extents (terrain / structure)
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
  for (int i = 0; i < 8; ++i)
    m.edges.push_back({i, (i + 1) % 8});
  for (int i = 1; i < 8; ++i)
    m.edges.push_back({0, i});
  m.display_list = {
    0x80E4, 0xA016, 0x80E4, 0xFFFF, 0x4014, 0x40E4, 0xC010, 0x40A0,
  };
  return m;
}

// ---------------------------------------------------------------------------
// Structure $00C3A4A8
// ---------------------------------------------------------------------------
inline Mesh mesh_structure_3A4A8() {
  Mesh m;
  m.name = "structure_$00C3A4A8";
  m.verts = {
    V(  512,    0, -1024),
    V(  -96, 1472,  -672),
    V(-1280,    0,  -768),
    V( -816, 1664,  -288),
    V(-2048,    0,  1408),
    V(  336, 3632,  1296),
    V( 1408,    0,   512),
    V(  576, 2816,  -336),
  };
  // Dump-derived: sequential 800C pairs form a 4-cycle on verts 0..3
  m.edges = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
  };
  m.edge_pens = {4, 4, 4, 4};
  m.display_list = {
    0x4380, 0x4070, 0xC00A, 0xE070, 0xC00A, 0x0012, 0x001E, 0x0038,
    0x8052, 0xC00C, 0x0000, 0x0006, 0x8018, 0x100B,
  };
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
  const bool per_edge = mesh.edge_pens.size() == mesh.edges.size();
  for (size_t ei = 0; ei < mesh.edges.size(); ++ei) {
    int a = mesh.edges[ei].first, b = mesh.edges[ei].second;
    if (a < 0 || b < 0 ||
        a >= static_cast<int>(ok.size()) ||
        b >= static_cast<int>(ok.size()))
      continue;
    if (!ok[a] || !ok[b]) continue;
    if (per_edge && mesh.edge_pens[ei] != 0xFF)
      gfx.pen = mesh.edge_pens[ei];
    else
      gfx.pen = static_cast<uint16_t>(pen);
    blitter_line_stub(ren, gfx, sx[a], sy[a], sx[b], sy[b]);
  }
}

// Draw the 2-D top-down hull (radar silhouette) in screen space.
// cx,cy = centre; scale maps raw hull coords → pixels.
inline void draw_hull_2d(SDL_Renderer* ren, GfxState& gfx, const Mesh& mesh,
                         int cx, int cy, float scale, int pen) {
  if (mesh.hull.size() < 2) return;
  gfx.pen = static_cast<uint16_t>(pen);
  auto px = [&](int16_t x) { return cx + static_cast<int>(x * scale); };
  auto py = [&](int16_t y) { return cy + static_cast<int>(y * scale); };
  for (size_t i = 0; i < mesh.hull.size(); ++i) {
    auto [x0, y0] = mesh.hull[i];
    auto [x1, y1] = mesh.hull[(i + 1) % mesh.hull.size()];
    blitter_line_stub(ren, gfx, px(x0), py(y0), px(x1), py(y1));
  }
}

}  // namespace fa18
