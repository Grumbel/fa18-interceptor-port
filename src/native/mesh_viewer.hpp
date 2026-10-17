// Offline aircraft mesh viewer (debug menu).
// Parses CODE-hunk clusters (same layout as tools/mesh_parse.py) and draws
// an orbitable wireframe on a host RGBA buffer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mesh_viewer {

struct Mesh {
  std::string label;
  int tags0 = 0, tags1 = 0;
  int pp = -1;  // -1 = none
  int declared_n = 0;
  std::vector<int16_t> vx, vy, vz;          // parallel arrays
  std::vector<std::pair<int, int>> edges;   // undirected pairs (i < j)
};

// Parse aircraft-tagged records from a hunk executable image.
std::vector<Mesh> load_from_exe(const std::vector<uint8_t>& exe);

// Interactive viewer.  Draws into `fb` (w*h RGBA).  Returns when the user
// closes (B/Back/Esc).  `poll` should pump SDL and return false on quit.
// `draw_text` matches main.cpp's 6x10 scaled text helper.
using DrawTextFn = void (*)(std::vector<uint32_t>&, int, int, int, int,
                            const std::string&, int, uint32_t);
using PollFn = bool (*)(void* user, int& key_dx, int& key_dy, int& key_orbit,
                        bool& close, bool& quit);

struct ViewInput {
  int mesh_delta = 0;   // -1 / +1 change mesh
  float yaw_delta = 0;  // radians this frame
  float pitch_delta = 0;
  float zoom_delta = 0; // multiplicative nudge
  bool close = false;
  bool quit = false;
};

// One frame of the viewer.  Maintains yaw/pitch/zoom/mesh_index via inout.
void render_frame(std::vector<uint32_t>& fb, int w, int h,
                  const std::vector<Mesh>& meshes, int& mesh_index,
                  float& yaw, float& pitch, float& zoom,
                  DrawTextFn draw_text);

}  // namespace mesh_viewer
