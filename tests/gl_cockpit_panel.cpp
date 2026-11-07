#include "native/cockpit_panel.hpp"

#include <array>
#include <cassert>
#include <cstdint>

int main() {
  using namespace remaster;
  using namespace remaster::cockpit_panel;
  static CockpitAtlas atlas;
  static std::array<uint32_t, 200 * 32> palette{};

  assert(!enabled(false, true, true));
  assert(!enabled(true, false, true));
  assert(!enabled(true, true, false));
  assert(enabled(true, true, true));

  // Outer borders and the verified fuel/radar separator only.
  for (const auto& xy : std::array<std::array<int, 2>, 6>{{
           {{0, 145}}, {{319, 199}}, {{128, 175}}, {{0, 180}}, {{100, 145}},
           {{100, 199}}
       }}) {
    const int x = xy[0], y = xy[1];
    const size_t cell = size_t(y) * CockpitAtlas::W + size_t(x);
    assert(structural(x, y));
    assert(!painted(atlas, x, y));
    atlas.valid[cell] = 1;
    atlas.index[cell] = 9;
    atlas.rgb[cell] = 0xff000000u;  // Even black ILBM cells are opaque.
    palette[size_t(y) * 32 + 9] = 0xff110000u | uint32_t(x << 8) | uint32_t(y);
    assert(painted(atlas, x, y));
    assert(*pixel(atlas, palette.data(), x, y) ==
           (0xff110000u | uint32_t(x << 8) | uint32_t(y)));
    assert(!pixel(atlas, nullptr, x, y));
  }

  // Explicitly leave original framebuffer-driven flight instruments alone.
  for (const auto& xy : std::array<std::array<int, 2>, 14>{{
           {{35, 155}}, {{70, 180}}, {{110, 160}}, {{110, 185}},
           {{143, 166}}, {{160, 174}}, {{177, 190}}, {{240, 175}},
           {{295, 180}}, {{292, 188}}, {{198, 183}}, {{225, 150}},
           {{313, 181}}, {{314, 190}}
       }}) {
    const int x = xy[0], y = xy[1];
    const size_t cell = size_t(y) * CockpitAtlas::W + size_t(x);
    atlas.valid[cell] = 1;
    atlas.index[cell] = 9;
    assert(!structural(x, y));
    assert(!painted(atlas, x, y));
    assert(!pixel(atlas, palette.data(), x, y));
  }
  assert(!structural(125, 144));
  assert(!structural(125, 200));
  assert(!structural(-1, 175));
  assert(!structural(320, 175));

  const size_t bad_index = size_t(175) * CockpitAtlas::W + 128;
  atlas.index[bad_index] = 32;
  assert(!painted(atlas, 128, 175));
}
