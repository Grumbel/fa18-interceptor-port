// Control-stick -> control-surface deflection processing for one aircraft
// record (player or AI), with the return-to-centre damping helpers.
// Original: hunk 0 $25584-$25e8a (compiled C), called from the assembly
// flight model.
//
// Aircraft record fields used (512-byte records at $5b47c, see
// handle_view_toggles): +2 flags word (bit 6 = elevator limit reached, bit 7
// = autopilot/hands-off, bit 8 = alternate stick table), +4 status byte,
// +32 handling flags, +38 / +102 / +106 / +108 / +110 state words, +40/+41/
// +42 raw stick (pitch, roll, rudder) bytes, +86 / +88 / +90 elevator,
// aileron and rudder deflections (words).
#include <cmath>
#include "native/globals.hpp"
#include "native/orig.hpp"
#include "native/vars.hpp"

namespace native {
using namespace g;
using namespace v;

namespace {
constexpr uint32_t scenario_table = 0x5b47c;
inline uint32_t R() { return rd32(0x2a6d8); }      // current record
inline int16_t W(uint32_t a) { return int16_t(rd16(a)); }
inline void SW(uint32_t a, int32_t v) { wr16(a, uint32_t(v) & 0xFFFF); }
inline int32_t ext(int16_t v) { return v; }
inline uint32_t u(int32_t v) { return uint32_t(v); }
}  // namespace

// $25b82: pull the deflection word at `p` towards zero: by value>>shift if
// large, else one unit (note: 0 becomes -1 like the original).
static void relax_toward_zero(uint32_t p, int16_t shift) {
  int16_t v = W(p);
  if (v < -15) {
    SW(p, v + ((-v) >> shift));
  } else if (v > 15) {
    SW(p, v - (v >> shift));
  } else if (v < 0) {
    SW(p, v + 1);
  } else {
    SW(p, v - 1);
  }
}
NATIVE_C(0x25b82, relax_toward_zero, false);

// $25e36: add speed-dependent trim step to a deflection (dead zone +-1280).
static void add_deflection_trim(uint32_t p) {
  int16_t step = int16_t(W(R() + 108) >> 7);
  int16_t v = W(p);
  if (v >= 0) {
    if (v < 1280) step = 0;
    SW(p, v + step);
  } else {
    if (v > -1280) step = 0;
    SW(p, v - step);
  }
}
NATIVE_C(0x25e36, add_deflection_trim, false);

// $25be6: elevator stall protection / return-to-trim (arg unused).
static void elevator_limit(int32_t /*who*/) {
  uint32_t rec = R();
  uint32_t p102 = rec + 102, p86 = rec + 86;
  int16_t v = W(p102);
  if (v <= 0) return;
  if (v < 400) {
    uint16_t flags = rd16(rec + 2);
    if (!(flags & 0x40)) {
      flags |= 0x40;
      wr16(R() + 2, flags);
      if ((rd16(R()) & 0x1600) == 0x1000 && !(rd8(R() + 4) & 2)) sub_2a194(15);
    }
    wr16(p86, 0);
  } else {
    int16_t s = (W(R() + 108) < 1728) ? 0 : 1;
    int16_t t = (W(p102) < 14400) ? -64 : 64;
    int16_t cur = W(p86);
    SW(p86, cur - ((cur - t) >> s));
  }
}
NATIVE_C(0x25be6, elevator_limit, false);

// $25cb2: rudder follows the turn direction ($+106 vs half circle).
static void rudder_follow(int32_t /*who*/) {
  uint32_t rec = R();
  uint32_t p106 = rec + 106, p90 = rec + 90;
  if (W(p106) != 0) {
    if (W(p106) > 14400) SW(p90, 32);
    else SW(p90, -32);
  } else {
    wr16(p90, 0);
  }
}
NATIVE_C(0x25cb2, rudder_follow, false);

// $25cf8: elevator deflection (+86) from stick value `a`, unless the
// aircraft is hands-off with a nonzero hold word and a <= 0.
static void drive_elevator(int16_t a) {
  uint32_t rec = R();
  if (!(rd16(rec + 2) & 0x80) && rd16(rec + 38) != 0 && a <= 0) return;
  rec = R();
  if (rd8(rec + 32) & 4) a = int16_t(a >> 1);
  uint32_t p = R() + 86;
  int16_t v = W(p);
  SW(p, v - ((v - a) >> 2));
  add_deflection_trim(p);
}
NATIVE_C(0x25cf8, drive_elevator, false);

// $25d62: aileron deflection (+88) from stick value `a` (scaled 5/8).
static void drive_aileron(int16_t a) {
  int16_t s = int16_t((a >> 1) + (a >> 3));
  if (rd8(R() + 32) & 4) s = int16_t(s >> 1);
  uint32_t p = R() + 88;
  int16_t v = W(p);
  SW(p, v - ((v - s) >> 2));
  add_deflection_trim(p);
}
NATIVE_C(0x25d62, drive_aileron, false);

// $25dbc: rudder deflection (+90) from value `a` (scaled 5/8).
static void drive_rudder(int16_t a) {
  int16_t s = int16_t((a >> 1) + (a >> 3));
  if (rd8(R() + 32) & 4) s = int16_t(s >> 1);
  int16_t k = (rd8(R() + 98) == 20) ? 2 : 1;
  uint32_t p = R() + 90;
  int16_t v = W(p);
  SW(p, v - ((v - s) >> k));
  add_deflection_trim(p);
}
NATIVE_C(0x25dbc, drive_rudder, false);

// $25584: process the stick of aircraft `w_5acac`: look up the response
// curve value for each axis and drive/relax the surfaces; also tracks the
// "elevator input" indicator bits in $5ae48/$5ae4b.
static void process_control_input() {
  int16_t idx = int16_t(w_5acac);
  uint32_t rec = scenario_table + (uint32_t(int32_t(idx)) << 9);
  wr32(0x2a6d8, rec);
  uint32_t pflags = rec + 2, p86 = rec + 86, p88 = rec + 88, p90 = rec + 90;
  uint32_t table = (idx == 0 || (rd16(pflags) & 0x100)) ? 337856 : 337980;
  wr16(pflags, rd16(pflags) & ~0x40);
  auto curve = [&](int16_t k) -> int16_t {      // signed response curve
    return k < 0 ? int16_t(-W(table + uint32_t(-int32_t(k)) * 2))
                 : W(table + uint32_t(int32_t(k)) * 2);
  };

  if (w_5abc4 & 0x40) {
    if (idx == 0) {
      if (l_5b4ee <= 0) {
        l_5b4ee = 0;
        if (!(rd8(0x5ae4b) & 0x40)) {
          l_5ae48 = int32_t((uint32_t(l_5ae48) & ~3u) | 0x40);
          wr16(pflags, rd16(pflags) & ~0x08);
          b_5ab36 = 2;
        }
      } else if (l_5b4ee < 460800) {
        if (!(rd8(0x5ae4b) & 2)) l_5ae48 = int32_t((uint32_t(l_5ae48) & ~0x41u) | 2);
      } else if (l_5b4ee < 768000) {
        if (!(rd8(0x5ae4b) & 1)) l_5ae48 = int32_t((uint32_t(l_5ae48) & ~0x42u) | 1);
      } else {
        l_5ae48 = int32_t(uint32_t(l_5ae48) & ~0x43u);
      }
    }
    rec = R();
    // Remaster: analog stick replaces the ramped digital deflections of the
    // player (same ranges as the ramp at $2e128: pitch +-20, rudder +-60).
    if (idx == 0 && mach().analog_enabled && mach().analog_active) {
      auto ax = [](float v, int lim) { return int8_t(std::lround(v * float(lim))); };
      wr8(rec + 40, uint8_t(ax(-mach().analog_y, 20)));   // up = positive
      wr8(rec + 42, uint8_t(ax(mach().analog_x, 60)));    // right = positive (bits $04)
    }
    int16_t k24 = int8_t(rd8(rec + 40));
    int16_t k26 = int8_t(rd8(rec + 41));
    int16_t k28 = int8_t(rd8(rec + 42));
    if (rd8(rec + 4) & 8) { k26 = 0; k28 = 0; }

    // ---- elevator (pitch) ------------------------------------------------
    if (k24 != 0) {
      w_5ae56 = curve(k24);
      if (!(rd16(pflags) & 0x80)) {
        drive_elevator(w_5ae56);
      } else if (rd8(R() + 32) & 1) {
        elevator_limit(idx);
        rudder_follow(idx);
      } else {
        uint32_t r = R();
        bool special = W(r + 108) > 768 && (W(r + 102) == 0 || W(r + 102) > 28000);
        if (special && k24 < 0) {
          int32_t m = (-int32_t(k24)) >> 1;
          drive_elevator(int16_t(-ext(W(table + uint32_t(m) * 2))));
        } else {
          elevator_limit(idx);
          rudder_follow(idx);
        }
      }
    } else {
      w_5ae56 = 0;
      k24 = 0;
      if (rd16(pflags) & 0x80) {
        elevator_limit(idx);
      } else if (W(p86) != 0 && rd16(R() + 38) == 0) {
        relax_toward_zero(p86, idx == 0 ? 2 : 1);
      }
    }

    // ---- aileron (roll) --------------------------------------------------
    if (k26 != 0) {
      w_5ae58 = curve(k26);
      if (!(rd8(R() + 4) & 2) && rd16(R() + 110) != 0) drive_aileron(w_5ae58);
      else relax_toward_zero(p88, 1);
    } else if (k28 == 0 || !(rd16(pflags) & 0x80)) {
      w_5ae58 = 0;
      k26 = 0;
      if (rd16(p88) != 0) relax_toward_zero(p88, 1);
    }

    // ---- rudder (yaw) ----------------------------------------------------
    if (k28 != 0) {
      if (rd16(pflags) & 0x80) {
        if (k28 >= 0) {
          if (k28 > 10) k28 = 10;
          w_5ae5a = W(table + uint32_t(int32_t(k28)) * 2);
        } else {
          if (k28 < -10) k28 = -10;
          w_5ae5a = int16_t(-W(table + uint32_t(-int32_t(k28)) * 2));
        }
      } else {
        w_5ae5a = curve(k28);
      }
      if (!(rd16(pflags) & 0x80)) {
        drive_rudder(int16_t(-ext(w_5ae5a)));
      } else {
        uint32_t r = R();
        int16_t v110 = W(r + 110), v106 = W(r + 106);
        if (!(rd8(r + 4) & 2) && v110 != 0) {
          drive_aileron(w_5ae5a);
          if ((v106 < 80 || v106 > 28720) && v110 > 864) {
            drive_rudder(int16_t(-ext(w_5ae5a)));
          } else if (v106 < 14400) {
            if (k28 < 0) wr16(p90, 0);
            else drive_rudder(int16_t(-ext(w_5ae5a)));
          } else {
            if (k28 > 0) wr16(p90, 0);
            else drive_rudder(int16_t(-ext(w_5ae5a)));
          }
        } else {
          relax_toward_zero(p88, 1);
          relax_toward_zero(p90, 1);
        }
      }
    } else {
      w_5ae5a = 0;
      k28 = 0;
      if (rd16(pflags) & 0x80) rudder_follow(idx);
      if (rd16(p90) != 0) relax_toward_zero(p90, 1);
    }
  } else {
    l_5ae48 = int32_t(uint32_t(l_5ae48) & ~0x43u);
  }

  // Elevator-input indicator for the player.
  if (idx == 0) {
    if ((w_5abc4 & 0x40) && l_5b4ee != 0) {
      if ((rd8(0x5b49c) & 4) && !(rd16(pflags) & 0x80)) {
        if (!(rd8(0x5ae4b) & 0x80)) {
          wr8(0x5ae4b, rd8(0x5ae4b) | 0x80);
          wr8(0x5ae4f, rd8(0x5ae4f) | 0x20);
        } else if (int16_t(w_5abe8) < 0) {
          w_5abe8 = 2;
        }
      } else if (rd8(0x5ae4b) & 0x80) {
        wr8(0x5ae4b, rd8(0x5ae4b) & 0x7F);
      }
    } else if (rd8(0x5ae4b) & 0x80) {
      wr8(0x5ae4b, rd8(0x5ae4b) & 0x7F);
    }
  }
}
NATIVE_C(0x25584, process_control_input, false);

}  // namespace native
