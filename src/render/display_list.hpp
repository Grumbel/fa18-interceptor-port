// Renderer-independent description of one presented frame of the 3D view.
//
// Produced by render::Shadow (the original scene code run on a copy of the
// game state), consumed by the OpenGL scene renderer.  Nothing in here
// refers to the 68000 machine, so a future C++ game core can fill the same
// structure.
//
// Conventions
// - Bitplanes: the original draws by plane operations (set / clear / invert
//   single bitplanes), not by colours.  A primitive therefore carries a
//   PlaneOp; the renderer keeps the palette index of every pixel and
//   resolves it through the palette at the end, so plane tricks (shadows,
//   water on land, ground dots) come out as in the original.
// - View space: x left, y up, z forward, already scaled by the original's
//   row scales, so the visible 4:3 frustum is |x| <= z, |y| <= z.  The
//   original maps it to column 159 - 160 x/z and row 89 - 90 y/z (points:
//   row 90 - 90 y/z) of the 320x200 screen, truncating; rows below the
//   cockpit edge are covered by the panel.
// - Distance: the view-space words of the original are in a fixed-point
//   unit of their own per object and level of detail, so equal numbers are
//   not equal distances.  Prim::unit says how many world units one unit of
//   the primitive's vertices is; with it a primitive can be seen from a
//   point beside the camera (the two eyes of a headset).
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace render {

struct PlaneOp {
  uint8_t set = 0, clr = 0, xr = 0;  // bit n = bitplane n
  // $45014: inside the primitive plane 2 := plane 3 (used for shading).
  bool copy3to2 = false;
  bool operator==(const PlaneOp&) const = default;
};

struct Prim {
  enum Kind : uint8_t {
    Poly,    // convex or concave polygon, `count` view-space vertices
    Line,    // 2 view-space vertices, one low-res pixel wide
    Point,   // 1 view-space vertex, w x h low-res pixels (1x1, 2x1, 2x2)
    Disc,    // 1 view-space vertex, radius in low-res pixels / rows
  } kind = Poly;
  PlaneOp op;
  uint32_t first = 0;   // index into Frame::verts
  uint16_t count = 0;
  uint8_t w = 1, h = 1; // Point
  int16_t radius = 0;   // Disc
  // World units per unit of this primitive's vertices; 0: not known, or a
  // direction only (the sun, screen-space fills): drawn at infinity.
  float unit = 0;
};

// World units (those of Camera) per metre: an altitude of 1024 units is
// 5 ft on the game's own altimeter.
constexpr float kUnitsPerMetre = 1024.f / (5.f * 0.3048f);

struct Vec3 {
  float x = 0, y = 0, z = 0;
};

struct Camera {
  // World position: sector * 2^22 + offset, world units; y is negative
  // above the sea (the original's convention).
  double x = 0, y = 0, z = 0;
  // World -> view rotation (orthonormal, row-major) and the row scales
  // ($5ad36/$5ad38/$5ad3a / 256) that hold the field of view.
  std::array<float, 9> rot{1, 0, 0, 0, 1, 0, 0, 0, 1};
  std::array<float, 3> scale{1, 1, 1};
  bool external = false;
};

// One eye of a head-mounted display.  Rotations are row-major, between
// spaces with x left, y up, z forward.
struct EyeView {
  std::array<float, 9> from_head{1, 0, 0, 0, 1, 0, 0, 0, 1};  // head -> eye
  // Where the eye is, in the head's axes and metres, from the point the
  // game's camera stands for (the head's place when the view was centred).
  std::array<float, 3> pos{0, 0, 0};
  // Tangents of the half angles, signed as in OpenXR (left and down negative).
  float tan_left = -1, tan_right = 1, tan_up = 1, tan_down = -1;
  int width = 0, height = 0;  // pixels of the eye's picture
};

struct Frame {
  Camera camera;
  // The primitives are in the view space of the quantised matrix the
  // original code used; `refine` (row-major) takes them to the view space
  // of the exact camera above (it only removes the fixed-point error of
  // the matrix, so it is close to the identity).
  std::array<float, 9> refine{1, 0, 0, 0, 1, 0, 0, 0, 1};
  // Horizontal field of view relative to the original's 4:3 view: the
  // frustum is |x| <= z in view space, which spans `widen` times the width
  // of the original picture.
  float widen = 1;
  // The same vertically: |y| <= z spans `heighten` times the original's
  // rows above and below the view centre (1 except for a headset).
  float heighten = 1;
  std::vector<Vec3> verts;
  std::vector<Prim> prims;       // painter's order
  // Ground: backdrop index (4 land, 6 open sea) and the altitude that
  // selects the water polygons (terrain::scaled_altitude).
  uint8_t backdrop = 4;
  int32_t ground_altitude = 0;
  bool water_all_planes = false; // $5aaa8: water in colour `water_colour`
  uint8_t water_colour = 6;
  // Rows of the 3D view in the 200-row screen (the cockpit covers the rest).
  int view_rows = 200;
  // The original's 2D drawing of this frame (cockpit, HUD, messages): the
  // palette index of each of the 320x200 pixels, and whether the pixel was
  // drawn at all (0: the 3D view shows through).  Empty if not available.
  std::vector<uint8_t> overlay_index, overlay_mask;
  // Of the drawn pixels, those of the HUD's symbols (1; the routines under
  // $47ec0 except its frame, drawn from $48e22), which a headset shows at infinity;
  // the rest (panel, nose, HUD frame, messages) is the cockpit.
  std::vector<uint8_t> overlay_hud;
  uint64_t game_frame = 0;       // machine frame of the newer snapshot
};

}  // namespace render
