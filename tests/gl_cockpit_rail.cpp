#include "native/cockpit_rail.hpp"

#include <array>
#include <cassert>
#include <cstdint>

int main() {
  using namespace remaster;
  static CockpitAtlas cockpit;
  static std::array<uint32_t, 200 * 32> palette{};
  const int left = 85, right = 233;
  assert(!cockpit_rail::front_lip(left, 137));
  assert(cockpit_rail::front_lip(left, 138));
  assert(cockpit_rail::front_lip(right, 144));
  assert(!cockpit_rail::front_lip(right, 145));
  assert(!cockpit_rail::front_lip(86, 140));

  for (int y = 138; y <= 144; ++y)
    for (int x : {left, right}) {
      const size_t cell = size_t(y) * CockpitAtlas::W + size_t(x);
      assert(!cockpit_rail::asset_background(cockpit, palette.data(), x, y));
      cockpit.valid[cell] = 1;
      cockpit.index[cell] = uint8_t((y + x) % 32);
      palette[size_t(y) * 32 + cockpit.index[cell]] =
          0xff000000u | uint32_t(y << 8) | uint32_t(x);
      assert(*cockpit_rail::asset_background(cockpit, palette.data(), x, y) ==
             (0xff000000u | uint32_t(y << 8) | uint32_t(x)));
      assert(!cockpit_rail::asset_background(cockpit, nullptr, x, y));
    }

  const size_t cell = 140 * CockpitAtlas::W + left;
  cockpit.index[cell] = 32;
  assert(!cockpit_rail::asset_background(cockpit, palette.data(), left, 140));
  assert(!cockpit_rail::asset_background(cockpit, palette.data(), left, 137));
  assert(!cockpit_rail::asset_background(cockpit, palette.data(), right, 145));
}
