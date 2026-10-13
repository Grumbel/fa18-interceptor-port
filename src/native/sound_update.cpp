// Per-frame engine/weapon/warning sound update: turns the flight state and
// the pending sound events into calls of the sound API (sound_api.cpp).
// Original: hunk 0 $24aa8 and helpers $252ce-$25580 (compiled C).
#include "native/globals.hpp"
#include "native/orig.hpp"
#include "native/vars.hpp"

namespace native {
using namespace g;
using namespace v;

namespace {
constexpr uint32_t scenario_table = 0x5b47c;
inline uint32_t current_record() { return rd32(0x2a6d8); }
inline int32_t sdiv(int32_t n, int32_t d) { return d == 0 ? 0 : int32_t(int64_t(n) / int64_t(d)); }
inline int32_t ext(int16_t v) { return int32_t(v); }
inline uint32_t u(int32_t v) { return uint32_t(v); }
}  // namespace

// $254ee: v/2 + v/8 (word arithmetic).
static int32_t scale_5_8(int16_t v) {
  int16_t r = int16_t((v >> 1) + (v >> 3));
  return r;
}
NATIVE_C(0x254ee, scale_5_8, false);

// $2550a: volume (1..63) of the current record's rotor/wind loop, from the
// magnitude of its word at +110 (fixed 16 for record type $30).
static int32_t wind_volume() {
  uint32_t rec = current_record();
  if ((rd8(rec + 98) & 0xF0) == 48) return 16;
  int16_t v = int16_t(rd16(rec + 110));
  if (v < 0) v = int16_t(-v);
  v = int16_t(v >> 9);
  if (v > 63) v = 63;
  if (b_5aa7d == 0) v = int16_t(v >> 1);
  if (v == 0) v = 1;
  return v;
}
NATIVE_C(0x2550a, wind_volume, false);

// $25316: engine volume curve for thrust-like value `v`, modified by the
// view (cockpit/outside) and external factors.
static int32_t volume_curve(int16_t v) {
  uint32_t rec = current_record();
  if (v != 0) {
    if (b_5aa8f != 0) v = 63;
    else if (v > 8) v = int16_t(63 - ((120 - v) >> 2));
    else v = int16_t(60 - ((120 - v) >> 1));
    if (b_5aa7d != 0) {
      if (v == 0) return v;
      if (b_5aaad != 0) {
        int16_t k = int8_t(b_5abaa);
        if (k > 4) {
          k = int16_t(8 - k);
          if (k < 0) k = 0;
        }
        if (uint32_t(int32_t(k)) < 4) {
          switch (k) {
            case 0: case 1: v = int16_t(scale_5_8(v)); break;
            case 2: v = int16_t(v - (v >> 2)); break;
            case 3: v = int16_t(v - (v >> 3)); break;
          }
        }
        int32_t m = int32_t(int8_t(b_5aa7d)) - 1;
        if (m >= 0 && m < 4) {
          switch (m) {
            case 0: v = int16_t(v - (v >> 3)); break;
            case 1: v = int16_t(v - (v >> 2)); break;
            case 2: v = int16_t(scale_5_8(v)); break;
            case 3: v = int16_t(v >> 1); break;
          }
        }
      } else if (w_5ae3a != 0) {
        v = int16_t(sdiv(int32_t(v) << 5, int16_t(w_5ae3a)));
        if (v > 84) v = 84;
      }
      if (!(rd16(rec + 2) & 8)) v = int16_t(v - (v >> 2));
      if (v <= 0) v = 1;
    } else {
      if (int8_t(b_5ab81) > 5) v = int16_t(v >> 1);
    }
  } else {
    if ((rd16(rec + 2) & 0x80) && rd16(rec + 110) != 0) v = 12;
  }
  return v;
}
NATIVE_C(0x25316, volume_curve, false);

// $252ce: rotor (sample 12) when available, otherwise engine sound b.
static void rotor_or_engine(int16_t pitch, int16_t level) {
  if (rd8(0x5ae52) & 0x02) {
    sub_2a260(u(ext(level)));
    b_5aa8f = 0;
  } else {
    sub_29e4e(u(ext(pitch) + 22), u(ext(level)));
  }
}
NATIVE_C(0x252ce, rotor_or_engine, false);

// $24aa8: the per-frame sound update.
static void update_sounds() {
  uint32_t rec = scenario_table + (uint32_t(int32_t(int16_t(w_5abd4))) << 9);
  wr32(0x2a6d8, rec);
  int8_t ev = int8_t(b_5aba8);
  uint32_t flags_ptr = rec + 2;
  if (b_5aba6 != 0 || (b_5aaa5 | b_5aaa6) != 0 || b_5aa8d == 0) return;

  // Pending one-shot sound requests ($5ab7d: bit 7 = start, 1 = stop).
  if (int8_t(b_5ab7d) < 0) {
    if (b_5aa7d != 0) {
      if (b_5aaad != 0) sub_2a1ee(5);
    } else {
      sub_2a1ee(10);
    }
    b_5ab7d = uint8_t(b_5ab7d & 0x7F);
  } else if (uint8_t(b_5ab7d - 1) == 0) {
    sub_2a254();
    b_5ab7d = 0;
  }

  // Weapon/impact events ($5ae4c != 0: something happened this frame).
  if (l_5ae4c != 0 && int8_t(b_5ab82) <= 0) {
    uint8_t e4e = rd8(0x5ae4e), e4f = rd8(0x5ae4f);
    if (e4f & 0x40) {
      sub_2a04a(160, 16, 8, 200, 16, 5, 3, 4, 4);
    } else if ((rd8(current_record() + 124) & 0x0F) == 0) {
      if (e4f & 0x80) sub_2a04a(324, 10, 8, 200, 10, 5, 3, 1, 4);
      else if (e4f & 0x01) { sub_2a04a(124, 16, 8, 180, 16, 5, 3, 3, 4); b_5ab82 = 3; }
      else if (e4f & 0x04) sub_2a04a(229, 3, 5, 229, 3, 5, 1, 0, 1);
      else if (e4e & 0x01) sub_2a04a(192, 4, 9, 200, 0, 3, 1, 0, 1);
      else if (e4f & 0x10) sub_2a04a(500, 15, 5, 500, 15, 5, 3, 1, 5);
      else if (e4f & 0x02) sub_2a04a(500, 10, 5, 130, 0, 1, 3, 3, 5);
      else if (e4f & 0x20) sub_2a04a(400, 8, 5, 330, 0, 1, 3, 2, 5);
      else if (e4e & 0x08) sub_2a04a(500, 10, 5, 130, 0, 1, 3, 1, 5);
      else if (e4f & 0x08) sub_29c60(3);
    }
    l_5ae4c = 0;
  }

  // Afterburner roar countdown.
  if (b_5aab0 != 0) {
    int8_t n = int8_t(b_5aab0 - 1);
    b_5aab0 = uint8_t(n);
    if (n <= 0) {
      int32_t a = int8_t(b_5ab62), b = int8_t(b_5ab61);
      if (b_5aa7d != 0) sub_2a0e4(u(a), u(b));
      else sub_2a0e4(u(a >> 1), u(b));
      b_5aab0 = 0;
    }
  }

  bool done = false;   // true: skip the engine block and clear the event
  if (ev == 0 && (b_5aab9 & 3) != 0) {
    // nothing to do this frame
  } else {
    rec = current_record();
    if ((rd8(rec + 98) & 0xF0) == 48) {
      sub_29fa2(u(wind_volume()));
      done = true;
    } else {
      int16_t a = int8_t(rd8(rec + 43));
      if (a < 0) a = int16_t(-a);
      int16_t base = 61;
      int16_t q = int16_t(volume_curve(a));
      if (q == 0) {
        sub_29fa2(u(wind_volume()));
        done = true;
      } else {
        int16_t t = int16_t(int16_t(rd16(rec + 90)) >> 4);
        if (t < 0) t = int16_t(-t);
        int16_t s = int16_t(int16_t(rd16(rec + 86)) >> 4);
        if (s < 0) s = int16_t(-s);
        if (t < s) t = int16_t(t + (int16_t(rd16(rec + 86)) >> 4));
        if (a >= 120) {
          a = int16_t(a - t);
        } else {
          a = int16_t(a + t);
          if (a > 120) a = 120;
        }
        int16_t pitch;
        if (b_5aa7d != 0 && b_5aba6 == 0) {
          q = int16_t(q - (q >> 2));
          if (b_5aaad == 0 && w_5ae3a != 0) {
            base = int16_t(sdiv(int32_t(base) << 5, int16_t(w_5ae3a)));
            if (base > 63) base = 63;
          }
          pitch = int16_t(800 - (a << 1) - (a >> 1) - (a >> 2));
          if (ev == -2) sub_29e4e(u(ext(pitch)), u(ext(q)));
          else if (ev == -3) sub_29e4e(u(ext(pitch) + 32), u(ext(base)));
          else if (ev == -4) sub_29e4e(u(ext(pitch) + 32), u(ext(q)));
          else if (ev == -5) rotor_or_engine(pitch, base);
          else if (ev == -6) {
            int16_t w8 = int16_t(base << 1);
            if (w8 > 63) w8 = 63;
            sub_29e4e(u(ext(pitch) + 22), u(ext(w8)));
          } else {
            sub_29f02(u(ext(pitch)), u(ext(q)), 32);
          }
        } else {
          if (q < 40) q = 40;
          pitch = int16_t(784 - (a << 1) - (a >> 2));
          if (ev == -2) {
            if (!(rd16(flags_ptr) & 8)) sub_29dba(u(ext(pitch)), u(ext(q)));
            else sub_29e4e(u(ext(pitch)), u(ext(q) >> 2));
          } else if (ev == -3) {
            sub_29e4e(u(ext(pitch) + 35), u(ext(base) >> 1));
          } else if (ev == -4) {
            sub_29dba(u(ext(pitch) + 32), u(ext(q)));
          } else if (ev == -5) {
            rotor_or_engine(pitch, base);
          } else if (ev == -6) {
            int16_t w8 = int16_t(base << 1);
            if (w8 > 40) w8 = 40;
            sub_29e4e(u(ext(pitch) + 22), u(ext(w8)));
          } else if (b_5aa8f != 0) {
            if (rd8(0x5ae53) & 0x01) sub_29ec6(u(ext(pitch)), 10, 32);
            else sub_29ec6(u(ext(pitch)), 40, 32);
          } else if (!(rd16(flags_ptr) & 8) || !(rd8(0x5ae53) & 0x01)) {
            sub_29ec6(u(ext(pitch)), u(ext(q)), 32);
          } else {
            int32_t x = ext(q);
            sub_29ec6(u(ext(pitch)), u((x >> 2) + (x >> 3)), 32);
          }
        }
        done = true;
      }
    }
  }
  if (done) b_5aba8 = 0;

  // Flap/boost request countdown.
  int8_t n = int8_t(b_5aa8f);
  if (n > 0) {
    n = int8_t(n - 1);
    b_5aa8f = uint8_t(n);
    if (n == 0 && b_5aa7d == 0 && !(rd16(flags_ptr) & 8)) b_5aba8 = uint8_t(-2);
  }
}
NATIVE_C(0x24aa8, update_sounds, false);

}  // namespace native
