// Concrete software ISceneRenderer: depth-sorts SceneFrame objects and
// rasterizes their captured prims into an RGB framebuffer, then composites
// with the original low-res frame so HUD / cockpit stay from the game.
//
// Enable with FA18_SCENE_RENDER=<scale> (1..8).  Implies scene capture.
// Does not alter game memory or the original draw path — it only consumes
// the read-only SceneFrame snapshot at end_frame.
//
// Presentation: Machine presents the buffer via frame_hires when
// compose_software() returns true (same host path as --hires).
#pragma once

#include "native/scene_frame.hpp"

#include <cstdint>
#include <vector>

namespace scene {

struct SoftwareSceneRenderer : ISceneRenderer {
  explicit SoftwareSceneRenderer(int scale);
  void on_scene(const SceneFrame& frame) override;

  int scale() const { return scale_; }
  int width() const { return width_; }
  int height() const { return height_; }
  const std::vector<uint32_t>& pixels() const { return pixels_; }
  const std::vector<uint8_t>& cover() const { return cover_; }
  uint64_t last_frame_id() const { return last_frame_id_; }
  bool has_frame() const { return has_frame_; }

 private:
  int scale_ = 1;
  int width_ = 0;
  int height_ = 0;
  int rows_ = 200;
  std::vector<uint32_t> pixels_;  // 3D-only colour where cover_ != 0
  std::vector<uint8_t> cover_;    // 1 = pixel written by a scene prim
  uint64_t last_frame_id_ = 0;
  bool has_frame_ = false;
};

// Auto-install when FA18_SCENE_RENDER is set.  Safe to call repeatedly.
void ensure_software_renderer();

// True when a software scene renderer is active.
bool software_render_enabled();

// Composite: upsample the machine's low-res RGB frame, then overlay the
// depth-sorted 3D pixels.  Sets w/h.  False if no scene frame yet.
bool compose_software(std::vector<uint32_t>& out, int& w, int& h);

}  // namespace scene
