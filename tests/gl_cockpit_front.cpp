#include "native/cockpit_front.hpp"

#include <array>
#include <cassert>
#include <cstdint>

int main() {
  using namespace remaster;
  static CockpitAtlas atlas;
  static std::array<uint32_t, 200 * 32> palette{};

  assert(!cockpit_front::painted(atlas, 16, 133));
  assert(!cockpit_front::painted(atlas, 15, 133));
  assert(!cockpit_front::painted(atlas, 304, 133));
  assert(!cockpit_front::painted(atlas, 16, 132));
  assert(!cockpit_front::painted(atlas, 16, 145));

  for (int y : {133, 138, 144}) {
    for (int x : {16, 85, 233, 303}) {
      const size_t c = size_t(y) * CockpitAtlas::W + size_t(x);
      atlas.valid[c] = 1;
      atlas.index[c] = 6;
      atlas.rgb[c] = 0xff505050u;
      const uint32_t ink = 0xff000000u | uint32_t(y << 8) | uint32_t(x);
      palette[size_t(y) * 32 + 6] = ink;
      assert(cockpit_front::painted(atlas, x, y));
      assert(*cockpit_front::pixel(atlas, palette.data(), x, y) == ink);
      assert(!cockpit_front::pixel(atlas, nullptr, x, y));
    }
  }

  // A nontransparent, *black* ILBM sample may leave the 3D background
  // visible. It must not become a solid black front-panel pixel.
  const size_t black = 140 * CockpitAtlas::W + 127;
  atlas.valid[black] = 1;
  atlas.index[black] = 0;
  atlas.rgb[black] = 0xff000000u;
  assert(!cockpit_front::painted(atlas, 127, 140));
  assert(!cockpit_front::pixel(atlas, palette.data(), 127, 140));

  const size_t invalid = 140 * CockpitAtlas::W + 128;
  atlas.rgb[invalid] = 0xff505050u;
  atlas.index[invalid] = 6;
  assert(!cockpit_front::painted(atlas, 128, 140));

  const size_t bad_index = 140 * CockpitAtlas::W + 129;
  atlas.valid[bad_index] = 1;
  atlas.rgb[bad_index] = 0xff505050u;
  atlas.index[bad_index] = 32;
  assert(!cockpit_front::painted(atlas, 129, 140));
}
