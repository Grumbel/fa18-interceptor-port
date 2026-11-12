// OpenGL 2.1 / OpenGL ES 2 renderer of a render::Frame.
//
// The picture is built like the original builds it: primitives in painter's
// order, each a bitplane operation.  The stencil buffer holds the palette
// index of every pixel (bit n = bitplane n; plane operations are stencil
// REPLACE / INVERT under a write mask), and one pass per palette entry
// turns the indices into colours at the end.  There is no depth buffer and
// therefore no near or far plane.
#pragma once

#include "render/display_list.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace terrain { struct Terrain; }

namespace render {

using GetProc = void* (*)(const char*);

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;  // window pixels, origin top left
};

class GlScene {
 public:
  GlScene();
  ~GlScene();
  // The context must be current and have a stencil buffer of 8 bits.
  bool init(GetProc proc, std::string& err);
  void set_terrain(const terrain::Terrain& t);

  // Where a picture with the original's 4:3 shape goes in a window, and the
  // full-height area a wider scene may use.
  static Rect centre_4x3(int win_w, int win_h);
  static Rect wide_area(int win_w, int win_h, float max_widen = 2.4f);

  void begin(int win_w, int win_h);  // clear the window
  // 3D view: `area` is what the scene may cover; frame.widen must be
  // area.w / (width of the 4:3 centre).  palette: 16 entries 0x00RRGGBB.
  void draw_scene(const Frame& f, const uint32_t* palette, const Rect& area);
  // A picture (top row first), stretched to `dst`.  ARGB as the machine's
  // frame buffer; with `blend` the alpha channel is used.
  void draw_image(int slot, const uint32_t* argb, int w, int h, const Rect& dst, bool blend,
                  bool smooth);
  // Window contents as RGB, top row first.
  bool read_window(std::vector<uint8_t>& rgb, int win_w, int win_h);

  struct Stats {
    int prims = 0, triangles = 0, batches = 0, water_triangles = 0;
  };
  Stats last_stats() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> p_;
};

}  // namespace render
