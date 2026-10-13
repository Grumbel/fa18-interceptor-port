// Per-aircraft engine/airspeed update: fuel use and the speed word (+108)
// driven by the throttle byte (+43), afterburner and speed-brake state.
// Original: hunk 0 $25edc (compiled C) with the helpers $269ce, $269fa and
// $26a3a; called from the assembly aircraft loop at $3a5a6.
//
// Record fields (512-byte records at $5b47c): +0 word flags (bit 11 = heavy
// load), +2 word state (bit 3 afterburner, bit 7 hands-off/AI, bit 10/13/14/15
// throttle gate flags), +4 byte status, +24 long, +32 byte handling flags,
// +38 word, +43 byte throttle position (-32..120), +57 byte engine spool
// nibbles, +86 elevator, +101 byte throttle mode (bits 0..1), +102/+106
// attitude words, +108 word airspeed, +110 word speed result, +114 long fuel,
// +120 word, +124 byte engine flags.
#include "native/globals.hpp"
#include "native/orig.hpp"
#include "native/vars.hpp"

namespace native {
using namespace g;
using namespace v;

namespace {
inline uint32_t R() { return rd32(0x2a6d8); }      // current record
inline int16_t W(uint32_t a) { return int16_t(rd16(a)); }
inline void SW(uint32_t a, int32_t v) { wr16(a, uint32_t(v) & 0xFFFF); }
inline int32_t ext(int16_t v) { return v; }
constexpr uint32_t scenario_table = 0x5b47c;
}  // namespace

// $269ce: move the word at +38 a eighth of the way towards `target`.
static void ease_word_38(int32_t target) {
  int16_t t = W(R() + 38);
  int16_t d = int16_t(int16_t(t - int16_t(target)) >> 3);
  SW(R() + 38, int16_t(t - d));
}
NATIVE_C(0x269ce, ease_word_38, false);

// $269fa: damp the word at `p`: v -= v >> shift while |v| > limit, else 0.
static void damp_word(uint32_t p, int32_t limit, int32_t shift) {
  int32_t v = W(p);
  int32_t lim = int16_t(limit);
  if (v > lim || v < -lim) {
    int16_t w = W(p);
    SW(p, int16_t(w - int16_t(w >> (int16_t(shift) & 63))));
  } else {
    wr16(p, 0);
  }
}
NATIVE_C(0x269fa, damp_word, false);

// $26a3a: drag-like term from the attitude words, the elevator deflection
// and the difference of the record's long at +24 to $5a9f2.
static int32_t drag_term() {
  uint32_t rec = R();
  int16_t a = W(rec + 102);
  if (a >= 14400) a = int16_t(a - 28800);
  a = int16_t(a >> 1);
  int16_t b = W(rec + 106);
  if (b >= 14400) b = int16_t(28800 - b);
  if (b >= 7200) b = int16_t(14400 - b);
  b = int16_t(b >> 3);
  int16_t e = W(rec + 86);
  if (e < 0) e = int16_t(-e);
  int16_t sum = int16_t(int16_t(e << 1) + b - a);
  int32_t diff = int32_t(l_5a9f2) - int32_t(rd32(rec + 24));
  if (rd16(rec + 2) & 8) diff >>= 11;
  else diff >>= 13;
  return int16_t(sum + diff);
}
NATIVE_C(0x26a3a, drag_term, false);

// $25edc: update engine state and airspeed of aircraft `w_5acac`.
static void update_aircraft_speed(uint32_t /*record pointer, unused*/) {
  int16_t idx = int16_t(w_5acac);
  uint32_t rec = scenario_table + (uint32_t(int32_t(idx)) << 9);
  wr32(0x2a6d8, rec);
  const uint32_t pflags = rec + 2, p4 = rec + 4, p32 = rec + 32, p43 = rec + 43;
  const uint32_t p101 = rec + 101, p108 = rec + 108, p57 = rec + 57;
  int16_t v24 = int16_t(-W(p108));   // -airspeed
  int16_t v40 = 0, v36, v28, v26, v22, v34, v38;

  if (rd32(rec + 114) != 0) {
    if (idx == 0) {
      v40 = int16_t(w_5aa74 - w_5aa70);
      if (v40 < 0) v40 = int16_t(-v40);
    }
    if ((rd8(p101) & 3) != 0) {
      v22 = int16_t((int8_t(rd8(p57)) & 15) + 1);
      if (v22 > 8) {
        v22 = 8;
      } else {
        wr8(p57, uint8_t((rd8(p57) & 0xF0) | uint16_t(v22)));
      }
      v22 = int16_t(v22 >> 1);
      if (v22 != 0 && rd8(p43) == 0 && int16_t(w_5abd4) == idx) b_5aba8 = uint8_t(-2);

      uint8_t mode = rd8(p101) & 3;
      if (mode == 1) {
        wr16(pflags, rd16(pflags) & ~0x400);
        int32_t s = int8_t(rd8(p43)) + ext(v22);
        wr8(p43, uint8_t(s));
        if (int8_t(s) >= 120) {
          uint16_t f = rd16(pflags);
          if (f & 0x2000) {
            if (!(f & 8)) {
              wr16(pflags, f | 8);
              b_5ab36 = 2;
              if (int16_t(w_5abd4) == idx) {
                b_5aba8 = uint8_t(-3);
                b_5aa8f = 6;
              }
            }
          } else {
            wr16(pflags, f | 0x2000);
            wr8(p101, rd8(p101) & 0xFC);
            if (idx == 0) b_5ab68 = 0;
          }
          wr8(p43, 120);
        }
      } else if (mode == 2) {
        uint16_t f = uint16_t(rd16(pflags) & ~0x2000);
        wr16(pflags, f);
        if (f & 8) {
          f = uint16_t(f & ~8);
          wr16(pflags, f);
          wr8(p43, uint8_t(rd8(p43) - 1));
          b_5ab36 = 2;
          if (int16_t(w_5abd4) == idx) b_5aba8 = uint8_t(-4);
        } else if (int8_t(rd8(p43)) >= 0) {
          uint8_t n = uint8_t(rd8(p43) - 4);
          wr8(p43, n);
          if (int8_t(n) < 0) {
            if (rd8(p4) & 8) {
              wr8(p43, 0);
            } else if (!(f & 0x400)) {
              wr8(p101, rd8(p101) & 0xFC);
              wr8(p43, 0);
              wr16(pflags, f | 0x400);
            }
          }
        } else {
          int32_t s = int8_t(rd8(p43)) - ext(v22);
          wr8(p43, uint8_t(s));
          if (int8_t(s) < -32) wr8(p43, uint8_t(-32));
        }
      }
      // player: mirror the throttle into the engine indicator word
      if (idx == 0) {
        v36 = int16_t(int8_t(rd8(p43)));
        if (v36 < 0) v36 = 0;
        if (b_5aad0 != 0 && (int16_t(w_5aa70) >= 960 || v40 < 16)) {
          int16_t t = int16_t(v36 << 3);
          w_5aa74 = t;
          if (int16_t(w_5aa70) >= 0) w_5aa70 = t;
        }
      }
    } else {
      uint8_t b = uint8_t(rd8(p57) & 0xF0);
      wr8(p57, uint8_t(b + 1));
    }
    if (idx == 0 && b_5ab9e != 2 && b_5aad0 != 0 &&
        (int16_t(w_5aa70) >= 960 || v40 > 16)) {
      int16_t p = int16_t(w_5aa70);
      if (p < 0) {
        w_5aa70 = 0;
        b_5ab68 = uint8_t(-1);
      } else if (rd16(pflags) & 8) {
        if (ext(p) < 864) b_5ab68 = uint8_t(ext(p) >> 3);
      } else if (p != 0) {
        if (rd8(p43) == 0 && int16_t(w_5abd4) == 0) b_5aba8 = uint8_t(-2);
        b_5ab68 = uint8_t(ext(p) >> 3);
      }
      w_5aa74 = int16_t(w_5aa70);
    }
  } else {
    wr8(p43, 0);
    if (idx == 0 && b_5aad0 != 0) {
      w_5aa74 = 0;
      w_5aa70 = 0;
    }
    wr16(pflags, rd16(pflags) & ~8);
  }

  // ---- airspeed ----------------------------------------------------------
  v36 = int16_t(int16_t(int32_t(int8_t(rd8(p43))) << 8));
  v28 = int16_t(-v24);
  if (v28 < 2048) v26 = 0;
  else v26 = int16_t(drag_term());

  if ((w_5abc4 & 0x40) && !(w_5abc6 & 0x4000)) {
    bool general = true;      // false: take the engine-off/coast path ($26758)
    if (v36 == 0 && !(rd16(pflags) & 8)) general = false;
    else if (rd32(R() + 114) == 0) general = false;

    if (general) {
      if (rd16(pflags) & 8) {
        // afterburner: burns fuel fast, speed limit from the drag term
        v38 = 1536;
        wr32(R() + 114, rd32(R() + 114) - uint32_t(ext(v38)));
        v22 = int16_t(15000 - v26);
        if (v22 > 18000) v22 = 18000;
        if ((rd8(R() + 124) & 0x70) == 0) v22 = int16_t(v22 - (v22 >> 2));
        if (rd16(rec) & 0x800) v22 = int16_t(v22 - (v22 >> 2));
        if (v28 < v22) {
          if (v24 > 0) {
            v24 = int16_t(v24 - 120);
          } else if (!(rd16(pflags) & 0x80)) {
            if (v28 > 7500) v24 = int16_t(v24 - 45);
            else v24 = int16_t(v24 - 54);
          } else {
            ease_word_38(-37);
            v24 = int16_t(v24 + W(R() + 38));
          }
        } else if (ext(v28) > ext(v22) + 60) {
          v24 = int16_t(v24 + 60);
        }
      } else {
        v38 = v36;
        if (v38 < 0) v38 = int16_t(-v38);
        v38 = int16_t(v38 >> 9);
        wr32(R() + 114, rd32(R() + 114) - uint32_t(ext(v38)));
        if (v36 >= 0) {
          v28 = int16_t(-v24);
          if (!(rd16(pflags) & 0x80)) {
            int16_t t = int16_t((v36 >> 1) - (v36 >> 3));
            v22 = int16_t(t - (t >> 2));
            if (rd8(p32) & 1) v22 = int16_t(v22 >> 2);
            if (v22 > 8175) v22 = 8175;
            v22 = int16_t(v22 - v26);
            if (rd16(rec) & 0x800) v22 = int16_t(v22 - (v22 >> 2));
            if ((rd8(R() + 124) & 0x70) == 0) v22 = int16_t(v22 - (v22 >> 2));
            if (v28 < v22) {
              if (v24 > 0) {
                v24 = int16_t(v24 - 120);
              } else if (ext(v22) - ext(v28) < 336) {
                v24 = int16_t(v24 - 5);
              } else {
                v24 = int16_t(v24 - 42);
              }
              v28 = int16_t(-v24);
              if (v28 > v22) v24 = int16_t(-v22);
            } else if (ext(v28) > ext(v22) + 60) {
              if (ext(v28) > 7500) v24 = int16_t(v24 + 82);
              else if (ext(v28) - ext(v22) < 480) v24 = int16_t(v24 + 7);
              else v24 = int16_t(v24 + 60);
            }
          } else {
            // hands-off: gentle thrust
            int16_t t = int16_t(v36 >> 3);
            v22 = int16_t(t - (t >> 2));
            if (!(rd8(p4) & 8) && int8_t(rd8(p43)) < 12) v22 = 0;
            if ((rd8(p4) & 0x44) == 0) v22 = int16_t(v22 >> 1);
            else if (rd8(p32) & 1) v22 = 0;
            v34 = 0;
            if (v28 < v22) {
              if (v24 > 0) v34 = -120;
              else if (rd8(p4) & 8) v34 = int16_t(-(int16_t(int16_t(v22 - v28) >> 8) + 66));
              else v34 = int16_t(-(int16_t(int16_t(v22 - v28) >> 8) + 22));
            } else if (ext(v28) > ext(v22) + 15) {
              v34 = 15;
            }
            ease_word_38(ext(v34));
            if (v34 != 0) v24 = int16_t(v24 + W(R() + 38));
          }
        } else {
          // negative throttle (reverse / brake)
          if (!(rd16(pflags) & 0x80)) {
            if (v24 < 0) {
              v24 = int16_t(v24 + 87);
            } else {
              int16_t t = int16_t(-v36);
              v22 = int16_t(t - (t >> 2));
              if (v24 > v22) v24 = int16_t(v24 - 42);
              else if (v24 < int16_t(ext(v22) - 42)) v24 = int16_t(v24 + 42);
            }
          } else {
            v34 = 0;
            if (v24 < 0) {
              v34 = 60;
            } else {
              int16_t t = int16_t(int16_t(-v36) >> 4);
              v22 = int16_t(t - (t >> 2));
              if (!(rd8(p4) & 4)) v22 = int16_t(v22 >> 1);
              if (v24 > v22) v34 = -22;
              else if (ext(v24) < ext(v22) - 22) v34 = 22;
            }
            ease_word_38(ext(v34));
            if (v34 != 0) v24 = int16_t(v24 + W(R() + 38));
          }
        }
      }
    } else {
      // no fuel or idle throttle: coast down
      if (!(rd16(pflags) & 0x80)) {
        if (v24 > 0) {
          if (v24 > 144) {
            if (v24 > 82) v24 = int16_t(v24 - 82);
            else v24 = 0;
          } else if (v24 > 60) {
            v24 = int16_t(v24 - 60);
          } else {
            v24 = 0;
          }
        } else {
          if (v28 > 7500) v24 = int16_t(v24 + 82);
          if (v28 < 1500) {
            if (v28 > 30) v24 = int16_t(v24 + 30);
          } else if (v28 > 60) {
            v24 = int16_t(v24 + 60);
          } else {
            v24 = 0;
          }
        }
      } else {
        ease_word_38(0);
        if (v24 > 0) {
          if (v24 > 15) v24 = int16_t(v24 - 15);
          else v24 = 0;
        } else if (v28 > 15) {
          v24 = int16_t(v24 + 15);
        } else {
          v24 = 0;
        }
      }
    }
  }

  // ---- store the speed ---------------------------------------------------
  v28 = int16_t(-v24);
  if (idx == 0) {
    if (v28 >= 8400) {
      uint16_t f = w_5abca;
      if (!(f & 0x4000)) {
        w_5abca = uint16_t(f | 0x4000);
        b_5aab8 = 3;
        b_5aba8 = uint8_t(-6);
        b_5aa8f = 2;
      }
    } else {
      w_5abca = uint16_t(w_5abca & ~0x4000);
    }
  }
  v22 = 0;
  uint16_t f = rd16(pflags);
  if ((f & 0xC000) == 0xC000) {
    damp_word(p108, 7, 3);
  } else if ((f & 0x80) && (rd16(rec) & 0x800) && W(p108) > 64) {
    damp_word(p108, 15, 4);
  } else if ((f & 0x80) && (rd8(R() + 124) & 0x80)) {
    damp_word(p108, 31, 5);
  } else if (rd8(p32) & 1) {
    damp_word(p108, 3, 2);
  } else if (rd8(p4) & 8) {
    v22 = 108;
  } else {
    SW(p108, -v24);
  }
  if (v22 != 0) {
    if (rd8(p4) & 8) {
      int32_t t = int8_t(rd8(p43));
      if (t >= ext(v22) || t <= -ext(v22)) SW(p108, -v24);
      else damp_word(p108, 7, 3);
    } else {
      SW(p108, -v24);
    }
  }
  sub_3ac1c();
  wr16(R() + 110, uint32_t(W(R() + 120) + W(p108)) & 0xFFFF);
}
NATIVE_C(0x25edc, update_aircraft_speed, false);

}  // namespace native
