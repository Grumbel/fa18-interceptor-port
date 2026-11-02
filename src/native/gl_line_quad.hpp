// Make a constant-pixel-width line out of two GL triangles in view space.
// Wide GL_LINES are optional on GLES2 and their coverage varies by driver;
// at a hi-res render scale they can appear as discontinuous low-res dashes.
#pragma once

#include <array>
#include <cmath>

namespace glr::line_quad {

struct Vec3 { double x, y, z; };

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

}  // namespace glr::line_quad
