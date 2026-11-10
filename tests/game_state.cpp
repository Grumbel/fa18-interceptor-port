// Unit test of the RAM -> GameState snapshot against a synthetic memory image.
#include "native/game_state.hpp"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

static std::vector<uint8_t> mem(0x70000);
static void w(uint32_t a, uint32_t v, int n) {
  for (int i = 0; i < n; ++i) mem[a + i] = uint8_t(v >> (8 * (n - 1 - i)));
}
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)

int main() {
  using namespace gamestate;
  w(0x5abd6, 512, 2);                       // player = scenario 1
  w(0x5ad5a, uint32_t(-1000), 4);
  w(0x5aa7d, 1, 1);
  w(0x5aed0 + 4, uint32_t(-60), 2);
  uint32_t a = kScenarioBase + 512;
  w(a, 0x11c8, 2);
  w(a + 20, 273963296, 4); w(a + 24, 1800, 4); w(a + 28, 279184624, 4);
  w(a + 102, uint32_t(-4000), 2); w(a + 104, 2240, 2); w(a + 106, 1440, 2);
  w(a + 43, uint32_t(-5), 1);
  w(a + 114, 24998u << 8, 4);
  w(kScenarioBase, 0x0010, 2);              // unused slot
  w(kEffectBase + 128 * 3 + 38, 1, 2);
  w(kEffectBase + 128 * 3 + 12, 77, 4);

  GameState s = read([](uint32_t ad, int n) -> uint32_t {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) v = v << 8 | mem[ad + i];
    return v;
  });
  CHECK(s.player == 1);
  CHECK(s.camera.pos.x == -1000);
  CHECK(s.camera.external);
  CHECK(s.camera.view_m[2] == -60);
  CHECK(!s.scenario[0].active);
  const Scenario& o = s.scenario[1];
  CHECK(o.active);
  CHECK(o.pos.x == 273963296 && o.pos.y == 1800 && o.pos.z == 279184624);
  CHECK(o.pitch == -4000 && o.heading == 2240 && o.bank == 1440);
  CHECK(o.throttle == -5);
  CHECK((o.fuel >> 8) == 24998);
  CHECK(s.effect[3].active && s.effect[3].vel.x == 77);
  CHECK(!s.effect[2].active);
  w(0x5abd6, 100, 2);
  CHECK(read([](uint32_t ad, int n) -> uint32_t {
          uint32_t v = 0;
          for (int i = 0; i < n; ++i) v = v << 8 | mem[ad + i];
          return v;
        }).player == -1);
  std::puts("game_state: OK");
  return 0;
}
