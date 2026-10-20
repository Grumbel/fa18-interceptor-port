// Debug overlay: game world statistics (see stats_overlay.hpp).  Field
// meanings come from the translated routines in src/native and
// docs/world_format.md; values are shown raw where the unit is unknown.
#include "stats_overlay.hpp"

#include "amiga/machine.hpp"

#include <cmath>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>

namespace stats_overlay {

namespace {

// Scenario records ($5b47c, 16 x 512 bytes; docs/world_format.md)
constexpr uint32_t records = 0x5b47c;
constexpr int record_count = 16;
constexpr uint32_t record_size = 512;
constexpr uint32_t effect_slots = 0x5af6a;  // 10 x 128 bytes (effects.cpp)

constexpr uint32_t white = 0xFFFFFFFFu, grey = 0xFFB0B0B0u, yellow = 0xFFFFD040u,
                   red = 0xFFFF6060u, green = 0xFF60FF60u, cyan = 0xFF60D0FFu;

std::string fmt(const char* f, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, f);
  vsnprintf(buf, sizeof buf, f, ap);
  va_end(ap);
  return buf;
}

// 28800 units = 360 degrees (record +102..106, docs/ground_sea_sky.md)
double degrees(int16_t a) { return double(a) / 80.0; }

// Cockpit read-outs ($47080 altimeter, $470f2 airspeed, fuel gauge)
int32_t feet(int32_t units) { return (units >> 10) * 5; }
int knots(int16_t speed_110) { return std::abs(int(speed_110)) / 12; }
double nautical_miles(double units) { return units * 5.0 / 1024.0 / 6076.0; }

// X/Z wrap at 22 bits (the camera code masks them with $3fffff)
int32_t wrap22(int32_t d) { return int32_t(uint32_t(d) << 10) >> 10; }

const char* kind_name(uint8_t kind, bool own) {
  if (own) return "player";
  switch (kind) {
    case 0x00: return "AIM-9";
    case 0x01: return "AIM-120";
    case 0x10: return "fighter";
    case 0x12:
    case 0x13: return "hostile";
    case 0x14: return "airliner";
    case 0x16: return "AF One";
    case 0x20: return "carrier";
    case 0x32: return "parachute";
    default: break;
  }
  switch (kind & 0xF0) {
    case 0x10: return "aircraft";
    case 0x20: return "ship";
    case 0x30: return "object";
    default: return "?";
  }
}

}  // namespace

std::vector<Line> build(amiga::Machine& m) {
  std::vector<Line> out;
  auto r8 = [&](uint32_t a) { return m.r8(a); };
  auto r16 = [&](uint32_t a) { return int16_t(m.r16(a)); };
  auto r32 = [&](uint32_t a) { return int32_t(m.r32(a)); };

  if (r8(0x5aa8d) == 0) return out;  // not in the 3D world

  const uint32_t own_off = uint32_t(int32_t(r16(0x5abd6)));
  const uint32_t own = records + own_off;
  const uint32_t log = uint32_t(r32(0x2d03c));  // flight log record

  out.push_back({fmt("MISSION %d  enemy level %d  enemies %d/%d  frame %d",
                     int8_t(r8(0x5ab9e)), int8_t(r8(0x5ab9f)),
                     int8_t(r8(0x5aba1)), int8_t(r8(0x5aba2)), r16(0x5abd2)),
                 yellow});

  // ---- player ---------------------------------------------------------------
  const int16_t flags = r16(own + 0), state = r16(own + 2);
  out.push_back({fmt("PLAYER slot %d  pos %d,%d,%d  cell %d,%d", own_off / record_size,
                     r32(own + 20), r32(own + 24), r32(own + 28), r16(own + 6), r16(own + 8)),
                 white});
  out.push_back({fmt("  alt %d ft  speed %d kt  fuel %d lb  throttle %d%s%s",
                     feet(r32(own + 24)), knots(r16(own + 110)), r32(own + 114) >> 8,
                     int8_t(r8(own + 43)),
                     (state & 0x08) ? "  AB" : "", (state & 0x80) ? "  on ground" : ""),
                 white});
  out.push_back({fmt("  heading %.1f  pitch %.1f  roll %.1f", degrees(r16(own + 104)),
                     degrees(r16(own + 102)), degrees(r16(own + 106))),
                 white});
  // +95: high nibble Sidewinders, low nibble AMRAAMs ($376c0); +96 gun
  const uint8_t msl = r8(own + 95);
  out.push_back({fmt("  AIM-9 %d  AIM-120 %d  gun %d  flares %d  chaff %d", msl >> 4, msl & 15,
                     r16(own + 96), int8_t(r8(0x5ab45)), int8_t(r8(0x5ab44))),
                 cyan});
  // Each missile hit is fatal or "minor damage" ($3a90e); +32 bit 1 = fatal
  const bool fatal = r8(own + 32) & 2;
  const int dstate = r8(own + 124) & 15;
  out.push_back({fmt("  damage: %s  state %d  flags %04x/%04x", fatal ? "FATAL HIT" : "none",
                     dstate, uint16_t(flags), uint16_t(state)),
                 fatal || dstate ? red : green});
  if (log) {
    out.push_back({fmt("  log: gun %d/%d  AIM-120 %d/%d  AIM-9 %d/%d  hits taken %d  crashes %d",
                       r16(log + 58), r16(log + 60), r16(log + 62), r16(log + 64),
                       r16(log + 66), r16(log + 68), r16(log + 70), r16(log + 72)),
                   grey});
  }

  int effects = 0;
  for (int i = 0; i < 10; ++i)
    if (r16(effect_slots + uint32_t(i) * 128 + 38) & 1) ++effects;

  // ---- scenario objects ---------------------------------------------------------
  std::vector<Line> objs;
  int enemies = 0;
  for (int k = 0; k < record_count; ++k) {
    const uint32_t r = records + uint32_t(k) * record_size;
    const uint16_t f = uint16_t(r16(r));
    if (!(f & 0x40)) continue;  // byte +1 bit 6: in use
    const bool is_own = r == own;
    const uint8_t kind = r8(r + 98);
    // class $10 counts as an enemy unless +1 bit 3 is set (spawn flag 15)
    const bool enemy = !is_own && (kind & 0xF0) == 0x10 && !(f & 0x08);
    if (enemy) ++enemies;
    const double dx = wrap22(r32(r + 20) - r32(own + 20));
    const double dy = r32(r + 24) - r32(own + 24);
    const double dz = wrap22(r32(r + 28) - r32(own + 28));
    const double dist = nautical_miles(std::sqrt(dx * dx + dy * dy + dz * dz));
    const int st = r8(r + 124) & 15;
    const uint8_t tgt = r8(r + 56);
    std::string target = tgt == 0xFF ? "route" : tgt >= 0x80 ? fmt("#%d", tgt - 0x80) : "-";
    objs.push_back({fmt("%2d %02x %-9s %6.1f %6d %5d %4.0f %3d %5d %-5s%s", k, kind,
                        kind_name(kind, is_own), dist, feet(r32(r + 24)), knots(r16(r + 110)),
                        degrees(r16(r + 104)), st, r16(r + 76), target.c_str(),
                        (f & 0x8000) ? " hidden" : ""),
                    is_own ? white : enemy ? red : grey});
  }
  out.push_back({fmt("OBJECTS %zu  enemies %d  effects %d/10", objs.size(), enemies, effects),
                 yellow});
  out.push_back({" #  k  type       nm     alt ft    kt  hdg st   +76 tgt", grey});
  out.insert(out.end(), objs.begin(), objs.end());
  return out;
}

}  // namespace stats_overlay
