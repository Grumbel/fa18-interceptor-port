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

namespace remaster { struct GpuLayers; }
namespace glr {

using GetProc = void* (*)(const char*);

// `make_current` makes the renderer's GL context current (the host may
// use GL itself).
bool init(GetProc proc, std::function<void()> make_current, std::string& err);
bool ready();               // initialised
bool active();              // initialised and enabled
void set_enabled(bool on);  // switch between OpenGL and the software layer

// When the SDL host presents from compose_view() (widescreen or >60 Hz),
// scene_end only finalises the captured scene; the output-size GL render
// occurs at presentation. Avoid a redundant 320*S readback per capture.
void set_deferred_scene_readback(bool on);
bool deferred_scene_readback();
void finish_capture();
// Keep the last completed 3D frame when the game leaves flight mid-frame
// (notably the Pause screen) without calling scene_end().
void cancel_capture();

// Plane operation of a primitive (see hires.cpp PlaneOps).
struct Ops {
  uint8_t set = 0, clr = 0, xr = 0;
  bool copy = false;  // $45014 plane copy: not representable, skipped
};

// Capture hooks; cheap no-ops unless active().
void scene_begin();                                          // top of game_frame
void begin_object(uint32_t mesh, uint32_t stream, bool distant);  // LOD chosen
// Called first in begin_object, also when this renderer is not in use: the
// translated object routines announce every model they are about to draw
// (the shadow machine of --renderer new groups its primitives by it).
void set_object_observer(std::function<void(uint32_t mesh, uint32_t stream, bool distant)> f);
void note_poly(const int16_t (*v)[3], int n, int shift, const Ops& o);  // $61286
void note_line(const double a[3], const double b[3], const Ops& o);
// kind 0: 1x1, 1: 2x1, 2: 2x2 low-res pixels, 3: disc of `radius` pixels
void note_point(const double p[3], int kind, int radius, const Ops& o);

// Render the frame into a hi-res layer of palette indices (w x h, top row
// first, h = rows * S).  Returns false when nothing was rendered.
// A wider `w` extends the horizontal field of view while preserving the
// original central 320*S pixels. `alpha` blends previous/current captured
// view-space geometry and ground camera without advancing game simulation.
bool render_layer(std::vector<uint8_t>& px, int w, int h, int S, float alpha = 1.f);
// Opt-in visible-context presentation: draw into the scene FBO and composite
// original 2D elements using tiny GL textures. No scene glReadPixels or
// SDL_UpdateTexture; the host swaps the window after a successful call.
// `layer_revision` identifies the latest emulated game frame. Multiple
// interpolated presentations within that frame share identical 2D source
// layers even when the 3D geometry/camera blend changes.
bool present_gpu(const remaster::GpuLayers& layers, uint64_t layer_revision,
                 int side, int S, float alpha,
                 int drawable_w, int drawable_h,
                 const uint8_t* hud_rgba = nullptr, bool hud_visible = false,
                 const uint8_t* pause_rgba = nullptr, bool pause_visible = false,
                 const uint8_t* cockpit_rgba = nullptr);
// Show a framebuffer that has no usable captured 3D scene (title, menu,
// transitions, original/software graphics mode) on the same visible GL
// window. The caller swaps the GL window after presenting; the original
// bitplanes remain the source texture until those UI screens are native.
// `pixels` are top-down ARGB8888, `logical_width` uses the 320x240 aspect
// convention of SDL_Renderer. Optional HUD is a top-left 960x720 RGBA layer.
bool present_bitmap_gpu(const uint32_t* pixels, int width, int height,
                        int logical_width, int drawable_w, int drawable_h,
                        const uint8_t* hud_rgba = nullptr);
// Debug/test-only: read the visible GL back buffer before swapping. Never
// called by the ordinary presentation path.
bool read_gpu_window(std::vector<uint8_t>& rgba, int w, int h);
bool interpolation_available();
uint64_t capture_serial();         // advances only when the game draws a new 3D scene
int capture_interval_vblanks();    // observed spacing between game scenes

}  // namespace glr
