#include "native/game_state.hpp"

#include <cstdio>

namespace gamestate {
namespace {
struct R {
  const Reader& rd;
  int16_t s16(uint32_t a) const { return int16_t(rd(a, 2)); }
  uint16_t u16(uint32_t a) const { return uint16_t(rd(a, 2)); }
  int32_t s32(uint32_t a) const { return int32_t(rd(a, 4)); }
  uint8_t u8(uint32_t a) const { return uint8_t(rd(a, 1)); }
  Vec3l v3(uint32_t a) const { return {s32(a), s32(a + 4), s32(a + 8)}; }
};
}  // namespace

GameState read(const Reader& rd) {
  R r{rd};
  GameState s;
  int16_t off = r.s16(0x5abd6);
  if (off >= 0 && off < kScenarioCount * kScenarioStride && off % kScenarioStride == 0)
    s.player = off / kScenarioStride;

  s.camera.pos = {r.s32(0x5ad5a), r.s32(0x5ad5e), r.s32(0x5ad62)};
  s.camera.eye = r.v3(0x5ad74);
  s.camera.external = r.u8(0x5aa7d) != 0;
  s.camera.zoom = int16_t(r.u16(0x5ad3a));
  for (int i = 0; i < 9; ++i) s.camera.view_m[i] = r.s16(0x5aed0 + 2 * i);

  for (int i = 0; i < kScenarioCount; ++i) {
    uint32_t a = kScenarioBase + uint32_t(i) * kScenarioStride;
    Scenario& o = s.scenario[i];
    o.index = i;
    o.flags0 = r.u16(a);
    o.flags2 = r.u16(a + 2);
    o.pos = {r.s32(a + 20), r.s32(a + 24), r.s32(a + 28)};
    o.pitch = r.s16(a + 102);
    o.heading = r.s16(a + 104);
    o.bank = r.s16(a + 106);
    o.airspeed = r.s16(a + 108);
    o.speed110 = r.s16(a + 110);
    o.fuel = r.s32(a + 114);
    o.throttle = int8_t(r.u8(a + 43));
    o.seat = r.u8(a + 98);
    o.lod_byte = r.u8(a + 123);
    for (int k = 0; k < 9; ++k) o.body_m[k] = r.s16(a + 146 + 2 * k);
    o.active = (o.flags0 & ~0x0010u) != 0;
  }
  for (int i = 0; i < kRecordCount; ++i) {
    uint32_t a = kRecordBase + uint32_t(i) * kRecordStride;
    s.record[i].index = i;
    for (int k = 0; k < kRecordStride; ++k) s.record[i].raw[k] = r.u8(a + k);
  }
  for (int i = 0; i < kEffectCount; ++i) {
    uint32_t a = kEffectBase + uint32_t(i) * kEffectStride;
    Effect& e = s.effect[i];
    e.index = i;
    e.pos = r.v3(a);
    e.vel = r.v3(a + 12);
    e.state = r.u16(a + 38);
    e.lifetime = r.u16(a + 40);
    e.active = (e.state & 1) != 0;
  }
  return s;
}

std::string to_text(const GameState& s) {
  std::string out;
  char b[512];
  auto add = [&](const char* fmt, auto... a) {
    std::snprintf(b, sizeof b, fmt, a...);
    out += b;
  };
  add("player %d\n", s.player);
  add("camera pos %d %d %d eye %d %d %d external %d\n", s.camera.pos.x, s.camera.pos.y,
      s.camera.pos.z, s.camera.eye.x, s.camera.eye.y, s.camera.eye.z, s.camera.external);
  add("%s", "view_m");
  for (int16_t v : s.camera.view_m) add(" %d", v);
  add("%s", "\n");
  for (const Scenario& o : s.scenario) {
    if (!o.active) continue;
    add("scenario %d flags %04x/%04x pos %d %d %d ang %d %d %d spd %d %d fuel %d thr %d seat %d lod %d\n",
        o.index, o.flags0, o.flags2, o.pos.x, o.pos.y, o.pos.z, o.pitch, o.heading, o.bank,
        o.airspeed, o.speed110, o.fuel >> 8, o.throttle, o.seat, o.lod_byte);
    add("%s", "  body_m");
    for (int16_t v : o.body_m) add(" %d", v);
    add("%s", "\n");
  }
  for (const Record& rc : s.record) {
    bool any = false;
    for (uint8_t v : rc.raw) any |= v != 0;
    if (!any) continue;
    add("record %d:", rc.index);
    for (uint8_t v : rc.raw) add(" %02x", v);
    add("%s", "\n");
  }
  for (const Effect& e : s.effect)
    if (e.active)
      add("effect %d state %04x life %d pos %d %d %d vel %d %d %d\n", e.index, e.state, e.lifetime,
          e.pos.x, e.pos.y, e.pos.z, e.vel.x, e.vel.y, e.vel.z);
  return out;
}

}  // namespace gamestate
