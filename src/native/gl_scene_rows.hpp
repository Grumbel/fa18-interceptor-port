// Shared Amiga screen-bank/3D-scene row selection for CPU and GPU presenters.
// The source cockpit keeps its live per-scanline copper colours. Only the
// widescreen *extensions* freeze palette changes below the original view.
#pragma once

#include <algorithm>
#include <cstdint>

namespace remaster::gl_scene_rows {

struct Mapping {
  int scene_y = 0;
  int side_palette_y = 0;
  bool side_scene = false;
  bool has_scene_row = false;
  bool left_ehb = false, right_ehb = false;
};

inline int last_palette_row(int ready_rows) {
  return std::clamp(ready_rows - 1, 0, 199);
}

// `source_scene_y` is the row matched to a completed 3D buffer, or -1.
// `flight` permits extending the last completed scene during temporary
// transition frames that have no matching displayed Amiga bitmap bank.
inline Mapping select(int y, int ready_rows, int source_scene_y, bool flight,
                      uint8_t left_idx, uint8_t right_idx) {
  const int last = last_palette_row(ready_rows);
  const bool scene = source_scene_y >= 0 && source_scene_y < ready_rows;
  const bool edges = scene && y < ready_rows;
  return {scene ? source_scene_y : y, std::min(y, last),
          flight || scene, scene,
          edges && (left_idx & 16) != 0,
          edges && (right_idx & 16) != 0};
}

}  // namespace remaster::gl_scene_rows
