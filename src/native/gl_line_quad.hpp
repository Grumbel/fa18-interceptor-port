// Make a constant-pixel-width line out of two GL triangles in view space.
// Wide GL_LINES are optional on GLES2 and their coverage varies by driver;
// at a hi-res render scale they can appear as discontinuous low-res dashes.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace glr::line_quad {

struct Vec3 { double x, y, z; };

// Preserve the original graphic weight at arbitrary render scales while
// avoiding both a barely visible single physical pixel and the old
// full-low-resolution-pixel-wide steps. Width is expressed in original
// 320x200 screen pixels, as expected by make().
inline double stroke_width(int scale) {
  const int s = scale > 0 ? scale : 1;
  return std::max(1.5 / double(s), 0.55);
}

// Coordinates use the original projection: screen x=-160*x/z, y=-90*y/z.
// Returns false for an exactly projected-zero-length line; draw a point then.
inline bool make(Vec3 a, Vec3 b, std::array<Vec3, 6>& out,
                 double width_lowres_pixels = 1.0) {
  const double dx = -160.0 * (b.x / b.z - a.x / a.z);
  const double dy = -90.0 * (b.y / b.z - a.y / a.z);
  const double len = std::hypot(dx, dy);
  if (len < 1e-8) return false;
  const double nx = -dy / len * width_lowres_pixels * 0.5;
  const double ny = dx / len * width_lowres_pixels * 0.5;
  const Vec3 da{-nx * a.z / 160.0, -ny * a.z / 90.0, 0};
  const Vec3 db{-nx * b.z / 160.0, -ny * b.z / 90.0, 0};
  auto plus = [](Vec3 p, Vec3 d) { return Vec3{p.x + d.x, p.y + d.y, p.z}; };
  auto minus = [](Vec3 p, Vec3 d) { return Vec3{p.x - d.x, p.y - d.y, p.z}; };
  const Vec3 ap = plus(a, da), am = minus(a, da);
  const Vec3 bp = plus(b, db), bm = minus(b, db);
  out = {ap, am, bp, am, bm, bp};
  return true;
}

// Camera-facing screen-pixel square. The original also renders distant dots
// as solid squares. Triangulating them lets the 3D scene share one ordered
// draw call instead of alternating GL_POINTS with every triangle batch.
inline std::array<Vec3, 6> square(Vec3 p, double width_lowres_pixels) {
  const double dx = width_lowres_pixels * p.z / 320.0;
  const double dy = width_lowres_pixels * p.z / 180.0;
  const Vec3 a{p.x - dx, p.y - dy, p.z};
  const Vec3 b{p.x + dx, p.y - dy, p.z};
  const Vec3 c{p.x + dx, p.y + dy, p.z};
  const Vec3 d{p.x - dx, p.y + dy, p.z};
  return {a, b, c, a, c, d};
}

}  // namespace glr::line_quad
