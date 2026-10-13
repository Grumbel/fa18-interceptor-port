// Ten 128-byte "effect" slots at $5af6a (weapon rounds / debris objects that
// are spawned from the player's aircraft and tracked in the world). Original:
// hunk 0 $272e4 (per-frame driver, called from the game frame at
// $21230/$2124a/$21276) with $27554 (per slot update), $277e0 (spawn slot),
// $27b06 and $27c2c (orientation set-up through the assembly $39d58).
// ($27a30, a spawn-from-record variant, is never called.)
//
// Slot fields: +0/+4/+8 world position, +12/+16/+20 velocity, +24/+28/+32
// body velocity, +36 word, +38 word state (bit 0 active, bit 1 flying free,
// bit 4 / bit 7 impact stages, bit 8 follow the player, bit 12/13 weapon
// kinds), +40 word lifetime, +46 word, +48/+50 words (tile coordinates).
#include "native/globals.hpp"
#include "native/orig.hpp"
#include "native/vars.hpp"

namespace native {
using namespace g;
using namespace v;

namespace {
inline uint32_t slot() { return rd32(0x2a6dc); }
inline int16_t W(uint32_t a) { return int16_t(rd16(a)); }
inline int32_t L(uint32_t a) { return int32_t(rd32(a)); }
inline void SW(uint32_t a, int32_t v) { wr16(a, uint32_t(v) & 0xFFFF); }
inline void SL(uint32_t a, int32_t v) { wr32(a, uint32_t(v)); }
inline int32_t ext(int16_t v) { return v; }
inline void set_d0(uint32_t v) { cpu().d[0] = v; }   // leftover value asm callees may read
constexpr uint32_t slots_base = 372586;               // $5af6a
constexpr uint32_t scenario_table = 0x5b47c;
}  // namespace

// $27c2c: turn (k, dx, dy, dz) into a direction through the assembly matrix
// routine $39d58 (results in $5ad44..$5ad48) and set the slot's velocity
// vectors from it.
static void set_slot_velocity(int32_t k, int32_t dx, int32_t dy, int32_t dz) {
  sub_39d58(uint32_t(ext(int16_t(k))), uint32_t(ext(int16_t(dx))), uint32_t(ext(int16_t(dy))),
            uint32_t(ext(int16_t(dz))));
  int32_t x = ext(int16_t(w_5ad44)), y = ext(int16_t(w_5ad46)), z = ext(int16_t(w_5ad48));
  uint32_t s = slot();
  SL(s + 12, x << 3);
  SL(s + 16, y << 3);
  SL(s + 20, z << 3);
  SL(s + 24, x);
  SL(s + 28, y);
  SL(s + 32, z);
  wr16(s + 36, 0);
  if (rd16(s + 38) & 0x3000) {
    SL(s + 12, L(s + 12) + L(0x5b4ba));
    SL(s + 16, L(s + 16) + L(0x5b4be));
    SL(s + 20, L(s + 20) + L(0x5b4c2));
    SL(s + 24, L(s + 24) + (L(0x5b4ba) >> 3));
    SL(s + 28, L(s + 28) + (L(0x5b4be) >> 3));
    SL(s + 32, L(s + 32) + (L(0x5b4c2) >> 3));
  }
}
NATIVE_C(0x27c2c, set_slot_velocity, false);

// $27b06: aim the slot at the position it should reach: the offset between
// the target ($5ad4a.. + player position) and the slot, in 1/256 units.
static void aim_slot(int32_t /*slot index, unused*/) {
  int16_t trim;
  switch (w_5abd2 & 3) {
    case 2: trim = 0; break;
    case 1: trim = 16; break;
    case 0: trim = 0; break;
    default: trim = -16; break;
  }
  uint32_t rec = scenario_table + (uint32_t(int32_t(int16_t(w_5acb0))) << 9);
  int16_t spd = int16_t(W(rec + 108) >> 6);
  uint32_t s = slot();
  int16_t k;
  uint16_t f = rd16(s + 38);
  if (f & 0x1000) k = 13;
  else if (rd16(s + 38) & 0x2000) k = 5;
  else k = int16_t(trim + 198 + spd);
  int32_t dx = ((L(0x5b490) & 0x3FFFFF) + int32_t(l_5ad4a)) - L(s);
  int32_t dy = (int32_t(l_5ad4e) + L(0x5b494)) - L(s + 4);
  int32_t dz = ((L(0x5b498) & 0x3FFFFF) + int32_t(l_5ad52)) - L(s + 8);
  set_slot_velocity(ext(k), ext(int16_t(dx >> 8)), ext(int16_t(dy >> 8)), ext(int16_t(dz >> 8)));
}
NATIVE_C(0x27b06, aim_slot, false);

// $277e0: spawn slot `i` at the player's aircraft: position from the player
// record, direction depending on the weapon kind.
static void spawn_slot(int32_t i) {
  uint32_t s = slot();
  int16_t a, b, c;
  if (rd16(s + 38) & 0x1000) {
    a = 0; b = -8; c = 0;
    wr16(s + 40, 30);
  } else if (rd16(s + 38) & 0x2000) {
    a = 0; b = -8; c = -56;
    wr16(s + 40, 60);
  } else {
    if (rd8(0x5b4de) == 17) { c = 56; b = 0; a = 0; }
    else { a = 6; b = 1; c = 24; }
    wr16(slot() + 40, 20);
  }
  auto M = [](uint32_t a_) { return ext(W(a_)); };
  int32_t x = (M(0x5b50e) * a + M(0x5b510) * b + M(0x5b512) * c) >> 6;
  l_5ad4a = x;
  int32_t y = (M(0x5b514) * a + M(0x5b516) * b + M(0x5b518) * c) >> 6;
  l_5ad4e = y;
  int32_t z = (M(0x5b51a) * a + M(0x5b51c) * b + M(0x5b51e) * c) >> 6;
  l_5ad52 = z;
  s = slot();
  wr16(s + 48, rd16(0x5b482));
  s = slot();
  wr16(s + 50, rd16(0x5b484));
  s = slot();
  SL(s, (L(0x5b490) & 0x3FFFFF) + x);
  SL(s + 4, y + L(0x5b494));
  SL(s + 8, (L(0x5b498) & 0x3FFFFF) + z);
  if (rd16(s + 38) & 0x3000) {
    b = -64; c = 0;
  } else {
    b = 64; c = 1024;
    switch (ext(w_5abd2) & 3) {
      case 2: b = int16_t(b + 5); break;
      case 1: b = int16_t(b - 3); break;
      case 0: b = int16_t(b + 3); break;
      default: break;
    }
  }
  l_5ad4a = (M(0x5b510) * b + M(0x5b512) * c) >> 4;
  l_5ad4e = (M(0x5b516) * b + M(0x5b518) * c) >> 4;
  l_5ad52 = (M(0x5b51c) * b + M(0x5b51e) * c) >> 4;
  w_5acb0 = 0;
  aim_slot(i);
}
NATIVE_C(0x277e0, spawn_slot, false);

// $27554: advance slot `i`: lifetime, wrap of the tile coordinates, impact
// tests, then hand over to the type specific renderer/mover.
static void update_slot(int32_t i) {
  uint32_t s = slot();
  uint32_t pf = s + 38;
  if (W(s + 40) > 0) {
    if (!(rd16(pf) & 2)) {
      set_d0(uint32_t(i));
      if (sub_3ae9e(uint32_t(i)) != 0) {
        wr16(pf, uint16_t((rd16(pf) & ~8) | 0x12));
        wr16(slot() + 40, 2);
      } else if (L(s + 4) < 0) {
        wr16(pf, uint16_t((rd16(pf) & ~0x18) | 2));
        wr16(slot() + 40, 2);
        int32_t d = L(slot() + 16) >> 8;
        w_5adc8 = int16_t(d);
        if (int16_t(d) != 0) {
          set_d0(uint32_t(ext(int16_t(i))));
          if (sub_3b462(uint32_t(ext(int16_t(i)))) != 0) wr16(pf, rd16(pf) | 0x80);
        }
      } else {
        // wrap the x tile coordinate (word +48) with the 4M-unit fraction
        if (L(s) < 0) {
          int16_t v = int16_t(W(slot() + 48) - 1);
          wr16(slot() + 48, uint16_t(v));
          if (v < 0) wr16(slot() + 48, 127);
          SL(s, L(s) + 4194304);
        } else if (L(s) >= 4194304) {
          int16_t v = int16_t(W(slot() + 48) + 1);
          wr16(slot() + 48, uint16_t(v));
          if (v > 127) wr16(slot() + 48, 0);
          SL(s, L(s) - 4194304);
        }
        if (L(s + 8) < 0) {
          int16_t v = int16_t(W(slot() + 50) - 1);
          wr16(slot() + 50, uint16_t(v));
          if (v < 0) wr16(slot() + 50, 127);
          SL(s + 8, L(s + 8) + 4194304);
        } else if (L(s + 8) >= 4194304) {
          int16_t v = int16_t(W(slot() + 50) + 1);
          wr16(slot() + 50, uint16_t(v));
          if (v > 127) wr16(slot() + 50, 0);
          SL(s + 8, L(s + 8) - 4194304);
        }
      }
    } else if (rd16(pf) & 0x100) {
      SL(s, L(s) + (L(0x5b4ba) >> 2));
      SL(s + 4, L(s + 4) + (L(0x5b4be) >> 2));
      SL(s + 8, L(s + 8) + (L(0x5b4c2) >> 2));
    }
    if (rd16(pf) & 0x1002) {
      set_d0(uint32_t(ext(int16_t(i))));
      sub_41578(uint32_t(ext(int16_t(i))));
    } else if (rd16(pf) & 0x2000) {
      set_d0(uint32_t(ext(int16_t(i))));
      sub_4150c(uint32_t(ext(int16_t(i))));
    } else {
      set_d0(uint32_t(ext(int16_t(i))));
      sub_41484(uint32_t(ext(int16_t(i))));
    }
  } else {
    wr16(pf, rd16(pf) & ~1);
    if (ext(int8_t(b_5ab84)) == ext(int16_t(i)) + 1) b_5ab84 = 0;
    if (ext(int8_t(b_5ab85)) == ext(int16_t(i)) + 1) b_5ab85 = 0;
  }
}
NATIVE_C(0x27554, update_slot, false);

// $272e4: per-frame driver: age the effects budget byte ($5b4b7), update all
// ten slots and spawn a new one on request (key bit / weapon release).
static void update_effects() {
  int8_t spawned = 0;       // fp-3
  int8_t any_live = 0;      // fp-1
  const uint32_t budget = 373943;   // $5b4b7: reload counter
  const uint32_t mode = 374009;     // $5b4f9: high nibble = rounds left
  set_d0(0);
  sub_44098();
  wr8(budget, uint8_t(rd8(budget) - 1));
  for (int16_t i = 0; i < 20; i = int16_t(i + 2)) {
    uint32_t s = slots_base + (uint32_t(ext(i)) << 6);
    wr32(0x2a6dc, s);
    if (b_5aab5 == 0 && b_5aaa6 == 0) wr16(s + 40, uint16_t(rd16(s + 40) - 1));
    uint32_t pf = s + 38;
    bool go;
    if (b_5aaa6 == 0 && (w_5abbe & 8)) go = true;
    else go = b_5ab83 != 0;
    if (go) {
      if (rd16(pf) & 1) {
        // slot already in use
      } else if (spawned == 0 && (rd8(mode) & 0xF0) != 0 && int16_t(w_5b4dc) >= 1) {
        spawned++;
        uint8_t m = rd8(mode);
        wr8(mode, uint8_t(((m & 0xF0) - 16) | (m & 15)));
        any_live++;
        wr16(config_buf + 58, uint16_t(rd16(config_buf + 58) + 3));
        b_5aabd = 1;
        wr16(pf, 1025);
        wr16(slot() + 46, 0);
        if (b_5ab83 != 0) {
          if (b_5aaa5 == 0) { set_d0(28); sub_2a0e4(28, 48); }
          if (uint8_t(b_5ab83 - 1) == 0) {
            wr16(pf, rd16(pf) | 0x2000);
            b_5ab85 = uint8_t(ext(i) + 1);
          } else {
            wr16(pf, rd16(pf) | 0x1000);
            b_5ab84 = uint8_t(ext(i) + 1);
          }
          b_5ab83 = 0;
        } else {
          if (b_5aaa5 == 0) {
            if (rd8(0x5ae53) & 0x20) {
              set_d0(6);
              if (b_5aa7d != 0) sub_2a2f8(6, 30);
              else sub_2a2f8(6, 22);
            } else {
              set_d0(28);
              sub_2a0e4(28, 48);
            }
          }
          int16_t n = int16_t(w_5b4dc - 3);
          w_5b4dc = n;
          if (n < 0) w_5b4dc = 0;
          b_5ab3b = 3;
          wr8(budget, 20);
        }
        spawn_slot(ext(i));
      } else if (int8_t(rd8(budget)) > 0) {
        any_live++;
      }
    }
    if (rd16(pf) & 1) update_slot(ext(i));
  }
  if (any_live == 0) {
    wr8(budget, 0);
    wr8(mode, uint8_t((rd8(mode) & 15) | 0x50));
  }
  (void)spawned;
}
NATIVE_C(0x272e4, update_effects, false);

}  // namespace native
