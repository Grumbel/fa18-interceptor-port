#include "native/cockpit_dynamic_audit.hpp"
#include "native/cockpit_panel.hpp"

#include <array>
#include <cassert>
#include <cstdint>

int main() {
  using namespace remaster;
  using namespace remaster::cockpit_dynamic_audit;
  static CockpitAtlas atlas;
  static std::array<uint8_t, CockpitAtlas::W * CockpitAtlas::H> idx{};
  static std::array<uint32_t, CockpitAtlas::W * CockpitAtlas::H> rgb{};

  // Every sampled dynamic zone stays outside the static panel ownership,
  // apart from the panel's fixed top and bottom border rows.
  for (const Region& zone : kRegions) {
    for (int y = zone.y0; y < zone.y1; ++y) {
      for (int x = zone.x0; x < zone.x1; ++x) {
        assert(!cockpit_panel::structural(x, y));
      }
    }
  }
  assert(!cockpit_panel::structural(203, 175));  // Artificial horizon.
  assert(!cockpit_panel::structural(314, 189));  // Purple status cell.

  const Region globe = kRegions[3];
  const std::size_t a = 174 * CockpitAtlas::W + 200;
  const std::size_t b = 177 * CockpitAtlas::W + 203;
  atlas.valid[a] = atlas.valid[b] = 1;
  atlas.index[a] = atlas.index[b] = 5;
  atlas.rgb[a] = atlas.rgb[b] = 0xff110000u;
  idx[a] = 5;
  rgb[a] = 0xff220000u;  // Palette-only change, not a new pixel shape.
  idx[b] = 9;
  rgb[b] = 0xff001100u;  // A true changed pixel.
  const Result r = inspect(atlas, idx.data(), rgb.data(), globe);
  assert(r.samples == 2);
  assert(r.index_differences == 1);
  assert(r.rgb_differences == 2);
  assert(r.full_differences == 1);
  assert(r.min_x == 203 && r.max_x == 203);
  assert(r.min_y == 177 && r.max_y == 177);

  assert(inspect(atlas, nullptr, rgb.data(), globe).samples == 0);
  assert(inspect(atlas, idx.data(), nullptr, globe).samples == 0);
  const Result unchanged = inspect(atlas, idx.data(), rgb.data(),
                                   Region{"empty", 200, 146, 200, 199});
  assert(unchanged.samples == 0 && unchanged.max_x == -1);
}
