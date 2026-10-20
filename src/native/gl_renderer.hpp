// Remaster: OpenGL renderer for the 3D view (`fa18 --gl N`).
//
// Draws the 3D view with a depth buffer at N times the resolution:
//   - the ground is rebuilt in world space: a land plane, the water polygons
//     of the terrain tables (terrain.cpp) and the sky colour above the
//     horizon (docs/ground_sea_sky.md), so the horizon follows from the
//     perspective;
//   - the objects are the primitives the original rasterises (polygons
//     $38c0c, lines $43a56/$4397c, points $4389c..), in view space.  Each
//     object vertex stage has its own fixed-point unit (render_body: world
//     = view << ($5adb0 + 8 - -6(a6)); distant objects: << (8 + -8(a6)));
//     scaled back to world units the primitives of all objects share one
//     depth buffer.  Using the original's primitives (instead of decoded
//     models) keeps everything the polygon ops do at run time: sub-parts
//     placed by ops (runway markings), animation and damage states.
//   - flat objects (runways, islands, ground patches) are drawn in the
//     original order on the ground layer; 3D objects use the depth test.
// Everything is flat-shaded polygons with palette indices as colours; the
// plane operations are resolved to colours (a land/water bit in the
// stencil buffer covers the ground tricks).  GL 2.1 or GLES 2.  The
// indices replace the hi-res layer of hires.cpp at the end of the scene, so
// the HUD / cockpit composition, palette fades and the menus work unchanged.
// The original renderer keeps running: game state is not affected.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace glr {

using GetProc = void* (*)(const char*);

// `make_current` makes the renderer's GL context current (the host may
// use GL itself).
bool init(GetProc proc, std::function<void()> make_current, std::string& err);
bool ready();               // initialised
bool active();              // initialised and enabled
void set_enabled(bool on);  // switch between OpenGL and the software layer

// Plane operation of a primitive (see hires.cpp PlaneOps).
struct Ops {
  uint8_t set = 0, clr = 0, xr = 0;
  bool copy = false;  // $45014 plane copy: not representable, skipped
};

// Capture hooks; cheap no-ops unless active().
void scene_begin();                                          // top of game_frame
void begin_object(uint32_t mesh, uint32_t stream, bool distant);  // LOD chosen
void note_poly(const int16_t (*v)[3], int n, int shift, const Ops& o);  // $61286
void note_line(const double a[3], const double b[3], const Ops& o);
// kind 0: 1x1, 1: 2x1, 2: 2x2 low-res pixels, 3: disc of `radius` pixels
void note_point(const double p[3], int kind, int radius, const Ops& o);

// Render the frame into a hi-res layer of palette indices (w x h, top row
// first, h = rows * S).  Returns false when nothing was rendered.
bool render_layer(std::vector<uint8_t>& px, int w, int h, int S);

}  // namespace glr
