// High-level snapshot of the game state, read straight from the emulated
// RAM (no observation of the rasteriser).  This is the input of the
// state-driven renderer: camera, aircraft/object pools, weapon "effect"
// slots and the player's flight data.  Layouts: docs/camera_objects.md,
// docs/mesh_format.md and the record comments in engine.cpp / effects.cpp.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace gamestate {

// Memory reader: size is 1, 2 or 4; big-endian, unsigned result.
using Reader = std::function<uint32_t(uint32_t addr, int size)>;

constexpr uint32_t kScenarioBase = 0x5b47c;  // 16 x 512
constexpr uint32_t kRecordBase = 0x5d47c;    // 16 x 32
constexpr uint32_t kEffectBase = 0x5af6a;    // 10 x 128
constexpr int kScenarioCount = 16, kScenarioStride = 512;
constexpr int kRecordCount = 16, kRecordStride = 32;
constexpr int kEffectCount = 10, kEffectStride = 128;

struct Vec3l { int32_t x = 0, y = 0, z = 0; };

struct Camera {
  Vec3l pos;                 // $5ad5a/e/62: -(eye), world units
  Vec3l eye;                 // $5ad74/78/7c
  int16_t zoom = 128;        // $5ad3a: LOD distance scale (128 = 1.0)
  bool external = false;     // $5aa7d != 0: chase / external camera
  std::array<int16_t, 9> view_m{};  // $5aed0 world -> view, 8.8 fixed
};

// One 512-byte scenario record (aircraft, ships, missiles, parachute).
struct Scenario {
  int index = 0;
  uint16_t flags0 = 0;       // +0  (bit 15: not drawn)
  uint16_t flags2 = 0;       // +2  (bit 3 afterburner)
  Vec3l pos;                 // +20/+24/+28
  // Attitude angles: 28800 units = 360 deg (verified against body_m).
  int16_t pitch = 0;         // +102
  int16_t heading = 0;       // +104
  int16_t bank = 0;          // +106
  int16_t airspeed = 0;      // +108
  int16_t speed110 = 0;      // +110
  int32_t fuel = 0;          // +114 (>> 8 = displayed value)
  int8_t throttle = 0;       // +43
  uint8_t seat = 0;          // +98 view/seat mode
  uint8_t lod_byte = 0;      // +123
  std::array<int16_t, 9> body_m{};   // +146 attitude basis
  bool active = false;       // slot in use (unused slots hold flags 0x0010)
};

struct Record {
  int index = 0;
  std::array<uint8_t, kRecordStride> raw{};
};

struct Effect {
  int index = 0;
  uint16_t state = 0;        // +38 (bit 0 active)
  uint16_t lifetime = 0;     // +40
  Vec3l pos;                 // +0/+4/+8
  Vec3l vel;                 // +12/+16/+20
  bool active = false;
};

struct GameState {
  int player = -1;           // scenario index from $5abd6, -1 if invalid
  Camera camera;
  std::array<Scenario, kScenarioCount> scenario;
  std::array<Record, kRecordCount> record;
  std::array<Effect, kEffectCount> effect;
};

GameState read(const Reader& rd);
std::string to_text(const GameState& s);


constexpr float kAngleUnitsPerTurn = 28800.f;

}  // namespace gamestate
