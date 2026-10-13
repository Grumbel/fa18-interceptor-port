// Per-aircraft position-rate and ground contact update: smooths the body
// rate words, integrates altitude (+24) and handles touchdown / crash /
// takeoff transitions. Original: hunk 0 $26b16 (compiled C) and its helper
// $27290; called from the assembly aircraft loop at $3a64e.
//
// Record fields (512-byte records at $5b47c): +0 word flags (bit 9 = already
// counted as crash/landing), +2 word state (bit 7 = on the ground, bit 12 =
// airborne marker), +4 byte status (bits 6-7 = carrier/arrest state), +24
// long altitude, +62/+66/+70 long velocity vector, +76/+78 words, +98 type,
// +110 word speed, +118 word, +124 byte engine flags, +150/+156/+162 words
// attitude inputs.
#include "native/globals.hpp"
#include "native/orig.hpp"
#include "native/vars.hpp"

namespace native {
using namespace g;
using namespace v;

namespace {
inline uint32_t R() { return rd32(0x2a6d8); }      // current record
inline int16_t W(uint32_t a) { return int16_t(rd16(a)); }
inline int32_t L(uint32_t a) { return int32_t(rd32(a)); }
inline void SL(uint32_t a, int32_t v) { wr32(a, uint32_t(v)); }
inline int32_t ext(int16_t v) { return v; }
constexpr uint32_t scenario_table = 0x5b47c;
}  // namespace

// $27290: low-pass step: (a + b) scaled by 1/4 (|a+b| > 4) or 1/2 (> 2),
// minus b.
static int32_t smooth_rate(int32_t a, int32_t b) {
  int16_t s = int16_t(int16_t(a) + int16_t(b));
  int16_t m = s;
  if (m < 0) m = int16_t(-m);
  if (m > 4) s = int16_t(s >> 2);
  else if (m > 2) s = int16_t(s >> 1);
  return int16_t(s - int16_t(b));
}
NATIVE_C(0x27290, smooth_rate, false);

// $26b16: update aircraft `w_5acac`.
static void update_vertical(uint32_t /*record pointer, unused*/) {
  int16_t idx = int16_t(w_5acac);
  uint32_t rec = scenario_table + (uint32_t(int32_t(idx)) << 9);
  wr32(0x2a6d8, rec);
  const uint32_t pflags = rec + 2, p4 = rec + 4, p32 = rec + 32;
  const bool player = idx == 0;
  const bool zoom = b_5aa98 != 0 && player;

  w_5ad98 = W(rec + 150);
  w_5ad9a = W(rec + 156);
  w_5ad9c = W(rec + 162);
  sub_39cc0(uint32_t(ext(int16_t(-W(rec + 110)))), uint32_t(ext(w_5ad98)), uint32_t(ext(w_5ad9a)),
            uint32_t(ext(W(rec + 162))));

  int16_t r2 = int16_t(w_5ad44), r4 = int16_t(w_5ad46), r6 = int16_t(w_5ad48);
  int16_t t8 = 0, t12 = 0, t10;
  if (!(rd16(pflags) & 0x80)) {
    t8 = int16_t(L(R() + 62) >> 2);
    t12 = int16_t(L(R() + 70) >> 2);
    if (zoom) {
      t8 = int16_t(L(R() + 62) >> 6);
      t12 = int16_t(L(R() + 70) >> 6);
    }
    r2 = int16_t(smooth_rate(ext(r2), ext(t8)));
    r6 = int16_t(smooth_rate(ext(r6), ext(t12)));
  }
  int32_t d = L(R() + 66) >> 2;
  t10 = int16_t(d);
  if (zoom) t10 = int16_t(int16_t(d) >> 4);
  r4 = int16_t(smooth_rate(ext(r4), ext(t10)));
  if (!(b_5aa97 & 1)) r4 = int16_t(r4 + W(R() + 118));

  int32_t vx = ext(r2) << 2, vy = ext(r4) << 2, vz = ext(r6) << 2;
  int32_t climb_step = 24576;
  if (zoom) {
    vx = ext(r2) << 6;
    vy = ext(r4) << 6;
    vz = ext(r6) << 6;
    climb_step = 393216;
  }
  SL(R() + 62, -vx);
  SL(R() + 66, -vy);
  SL(R() + 70, -vz);
  if (b_5aaa6 != 0) {
    climb_step = 0;
    vy = 0;
  }
  if (player) {
    if (uint8_t(b_5aa97) == 3) vy -= climb_step;
    else if (uint8_t(b_5aa97) == 1) vy += climb_step;
  }

  // lowest altitude the aircraft may reach (ground / deck level)
  int32_t floor_alt;
  if ((rd8(p4) & 0xC0) != 0) {
    int32_t base;
    if ((rd8(R() + 124) & 0x70) == 0) base = (rd8(R() + 98) == 20) ? 2700 : 1800;
    else base = 750;
    floor_alt = (ext(W(R() + 78)) << 8) + base;
    if (floor_alt < base) floor_alt = base;
  } else if ((rd8(R() + 98) & 0xF0) == 0) {
    floor_alt = -512;
  } else if ((rd8(R() + 124) & 0x70) == 0) {
    floor_alt = (rd8(R() + 98) == 20) ? 2700 : 1800;
  } else {
    floor_alt = 750;
  }

  int32_t alt = L(R() + 24) - vy;
  SL(R() + 24, alt);

  if (alt <= floor_alt) {
    // ---- ground contact ---------------------------------------------------
    wr8(p32, rd8(p32) & ~4);
    if ((rd8(R() + 98) & 0xF0) != 0) SL(R() + 24, floor_alt);

    if ((rd8(p4) & 0xC0) == 0 && !((rd16(pflags) & 0x80) && (W(R() + 76) & 7) != 0)) {
      sub_3f842();
      if (rd8(p4) & 2) {
        uint8_t e = rd8(R() + 124);
        if ((e & 0x70) == 0) {
          wr8(R() + 124, e | 0x80);
          if (player) wr8(p32, rd8(p32) | 1);
        }
      }
    }

    if (!player) {
      wr16(pflags, rd16(pflags) | 0x80);
    } else {
      if (!(rd16(rec) & 0x200)) {
        // (the original has dead code here for the bit-9-set case)
        if (!(rd16(pflags) & 0x80)) {
          // first touchdown frame
          wr16(pflags, rd16(pflags) | 0x80);
          wr16(R() + 38, 0);
          sub_28e5c();
          l_5ac04 = l_5adea.get();
          b_5aa97 = 0;
          b_5aab8 = 1;
          if (r4 >= 576) {
            if (b_5ab92 == 0) {
              wr16(rec, rd16(rec) | 0x200);
              wr16(config_buf + 70, uint16_t(rd16(config_buf + 70) + 1));
              b_5aabd = 1;
              b_5aa90 = 4;
            }
          } else if (w_5b4ea != 0) {
            w_5b4e8 = w_5b4ea.get();
            b_5aba8 = uint8_t(-2);
            if ((rd8(p4) & 2) || (rd8(R() + 124) & 0x70) != 0) {
              if (W(R() + 110) >= 960) {
                wr16(pflags, rd16(pflags) | 0x1000);
                w_650c2 = w_5acae.get();
              }
            }
            if (r4 > 384) sub_2a194(40);
            else sub_2a194(30);
          }
        } else if (w_5b4ea == 0) {
          // rolling on the ground
          if (!(w_5abca & 2)) {
            b_5aab8 = 4;
            if (w_5abc2 & 0x100) {
              w_5abc2 = uint16_t(w_5abc2 & ~0x100);
              if ((rd8(p4) & 4) || (rd16(pflags) & 0xC000) == 0xC000) sub_2f156();
            }
            w_5abca = uint16_t(w_5abca | 2);
          }
          if (l_5b4ee == 0) w_5abc6 = uint16_t(w_5abc6 | 0x4000);
        } else if (w_5abca & 2) {
          b_5aab8 = 3;
          wr8(0x5ae4e, rd8(0x5ae4e) | 2);
          w_5abca = uint16_t(w_5abca & ~2);
        }
      }
      l_5b4be = 0;
    }
    if (W(R() + 110) < 960) wr16(pflags, rd16(pflags) & ~0x1000);
  } else {
    // ---- airborne ---------------------------------------------------------
    uint16_t f = rd16(pflags);
    if (f & 0x80) {
      wr16(pflags, f & ~0x80);
      if (rd8(p4) & 8) {
        wr8(p4, rd8(p4) & ~8);
        b_5aab8 = 4;
      }
      if (player) {
        sub_28e5c();
        int32_t dt = l_5adea - l_5ac04;
        l_5ac04 = dt;
        l_5ac08 = l_5ac08 + dt;
        l_5ac04 = 0;
        wr8(R() + 33, rd8(R() + 33) | 1);
        wr16(pflags, rd16(pflags) & ~0x1000);
        w_5abca = uint16_t(w_5abca & ~2);
        w_5abc2 = uint16_t(w_5abc2 | 0x100);
      } else {
        sub_2f11a();
      }
      wr8(p4, rd8(p4) & ~2);
    } else if (L(R() + 24) > int32_t(l_5a9f2)) {
      SL(R() + 24, l_5a9f2);
    }
  }
}
NATIVE_C(0x26b16, update_vertical, false);

}  // namespace native
