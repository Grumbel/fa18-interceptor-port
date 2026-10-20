// Debug overlay: game world statistics read from the game's memory (mission,
// player aircraft, weapons, scenario objects).  Read-only: it never changes
// game state, so runs with and without it are identical.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace amiga {
class Machine;
}

namespace stats_overlay {

struct Line {
  std::string text;
  uint32_t color;  // ARGB
};

// The overlay text for the current frame (empty outside the 3D world).
std::vector<Line> build(amiga::Machine& m);

}  // namespace stats_overlay
