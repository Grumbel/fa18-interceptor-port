// Software ISceneRenderer: render the captured SceneFrame at N× resolution.
//
// This is a real hi-res path driven only from the read-only SceneFrame
// snapshot — not a composite over the original low-res 3D.  Missing prims
// or incomplete capture show up as missing geometry; that is intentional.
//
// Presentation replaces the 3D view rectangle with the rendered buffer.
// Only rows below the view (HUD / cockpit) are taken from the low-res frame.
//
// Enable: FA18_SCENE_RENDER=<scale> (1..8).  Implies scene capture.
// Original game memory and cycles are untouched.
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
  int height() const { return height_; }  // 3D view height in hi-res pixels
  const std::vector<uint32_t>& pixels() const { return pixels_; }
  uint64_t last_frame_id() const { return last_frame_id_; }
  bool has_frame() const { return has_frame_; }

 private:
  int scale_ = 1;
  int width_ = 0;
  int height_ = 0;
  int rows_ = 200;  // low-res view rows
  std::vector<uint32_t> pixels_;  // full rendered 3D view
  uint64_t last_frame_id_ = 0;
  bool has_frame_ = false;
};

void ensure_software_renderer();
bool software_render_enabled();

// Present the rendered 3D view; HUD rows below the view come from low-res.
bool compose_software(std::vector<uint32_t>& out, int& w, int& h);

}  // namespace scene
