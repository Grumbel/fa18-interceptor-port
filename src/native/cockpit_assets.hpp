// Static cockpit artwork, decoded from the user's original ADF at runtime.
// No copyrighted image data is embedded in the native port.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace amiga { class Machine; }

namespace remaster {

struct CockpitAtlas {
  static constexpr int W = 320, H = 200;
  // Indexed in original 320x200 Amiga screen coordinates. `valid` is zero
  // where neither of the two original images has opaque artwork.
  std::array<uint8_t, W * H> index{};
  std::array<uint8_t, W * H> valid{};
  std::array<uint32_t, W * H> rgb{};  // ILBM colour map, 4-bit -> Amiga 8-bit
  // RGBA8 GPU texture: RGB=asset colour, A=original palette index (0..31).
  // Visibility is carried by the dynamic compositor's 2D overlay mask.
  std::vector<uint8_t> rgba;
  int opaque = 0;
};

const CockpitAtlas& cockpit_atlas(const amiga::Machine& m);
void reset_cockpit_atlas();

}  // namespace remaster
