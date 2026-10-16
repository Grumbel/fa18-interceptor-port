// Game flow: the per-frame driver and the state machine behind the mission
// selection screen, scenario start/end and the flight log.
// Original: hunk 0 $2112c-$23d08 (compiled C), translated from the assembly.
//
// The current state is a code address in game_state ($2a6d4); each frame
// run_game_state() decrements state_timer and calls the state handler.
// Handlers are registered at their original addresses so native and
// original handlers can be mixed (the state value stays an address).
#include "native/globals.hpp"
#include "native/orig.hpp"
#include "native/os.hpp"
#include "native/vars.hpp"
#include "native/asmregs.hpp"
#include "native/runtime.hpp"

namespace native {
using namespace g;
using namespace v;
using namespace asmr;

// ---- state machine ------------------------------------------------------------

inline constexpr Var<uint32_t> game_state{0x2a6d4};    // handler address
inline constexpr Var<int16_t> state_timer{0x5adce};    // frames, counts down
inline constexpr Var<int16_t> frame_counter{0x5abd2};
inline constexpr Var<int16_t> debug_stage{0x5adcc};    // progress marker in game_frame
inline constexpr Var<int8_t> in_flight{0x5aa8d};       // 3D world is running
inline constexpr Var<int16_t> message_queue{0x5aa42};  // [] message ids, 0-terminated
inline constexpr Var<int32_t> message_y{0x5aa36};
inline constexpr Var<int8_t> scenario{0x5ab9e};        // selected flight/mission

NATIVE_STATE(0x2196a, st_boot_check);
NATIVE_STATE(0x21a78, st_restart);
NATIVE_STATE(0x21a9e, st_21a9e);
NATIVE_STATE(0x21acc, st_21acc);
NATIVE_STATE(0x21aea, st_21aea);
NATIVE_STATE(0x21b5c, st_21b5c);
NATIVE_STATE(0x21ba4, st_21ba4);
NATIVE_STATE(0x21bd8, st_21bd8);
NATIVE_STATE(0x21cc8, st_21cc8);
NATIVE_STATE(0x21d0e, st_21d0e);
NATIVE_STATE(0x21d38, st_mission_menu);

// $39510: select a mission-specific message/value and place it in the
// message queue. The first argument selects a record through the signed
// offset table at $540e2; the third selects a word within that record, and
// the second is the destination slot in the queue at $5aa42. The original
// clears the following queue word to terminate the queue.
//
// Leftovers match the original: a0=$54006, a1=$540e2, a2=$5aa42,
// d1 = selected value, d0 = queue_slot*2 (word arithmetic on the long).
static void set_mission_message() {
  // C stack args under the return address (no link in the native path).
  const uint32_t sp = cpu().a[7];
  const uint32_t mission = rd32(sp + 4);
  const uint32_t queue_slot = rd32(sp + 8);
  const uint32_t record_slot = rd32(sp + 12);

  A(0) = 0x54006;
  A(1) = 0x540e2;

  // movel mission,d0; subqw #3,d0; addw d0,d0; a2 = a0 + a1@(d0.w)
  D(0) = mission;
  setw(0, lo(0) - 3);
  setw(0, lo(0) + lo(0));
  A(2) = A(0) + uint32_t(int16_t(rd16(A(1) + uint32_t(int16_t(lo(0))))));

  // movel record_slot,d0; addw d0,d0; d1 = a2@(d0.w)
  D(0) = record_slot;
  setw(0, lo(0) + lo(0));
  setw(1, rd16(A(2) + uint32_t(int16_t(lo(0)))));

  // movel queue_slot,d0; addw d0,d0; a2 = $5aa42; store & terminate
  D(0) = queue_slot;
  setw(0, lo(0) + lo(0));
  A(2) = 0x5aa42;
  wr16(A(2) + uint32_t(int16_t(lo(0))), lo(1));
  wr16(A(2) + uint32_t(int16_t(lo(0))) + 2, 0);
}
NATIVE_ASM(0x39510, set_mission_message, true);

// $397a2: append one hexadecimal digit from d1 to the byte string at a0.
// Values 0..9 become '0'..'9'; values 10..15 become 'A'..'F'.  The
// original uses word arithmetic for the ASCII conversion and postincrements
// a0 as part of the final move.b.  This helper is used while preparing the
// mission/status text shown between screens.
static void append_hex_digit() {
  uint8_t digit = asmr::lob(1);
  if (int8_t(digit) > 9) asmr::setw(1, asmr::lo(1) + 7);
  asmr::setw(1, asmr::lo(1) + 0x30);
  wr8(asmr::A(0), asmr::lob(1));
  ++asmr::A(0);
  asmr::flags(asmr::lob(1), 8);  // MOVE.B sets N/Z and clears V/C.
}
NATIVE_ASM(0x397a2, append_hex_digit, true);

// ---- Pure helpers (batch) --------------------------------------------------------

// $399f6: refresh airspeed-derived limits when $5adf2 >= 0.
static void refresh_airspeed_limits_399f6() {
  if (int32_t(rd32(0x5adf2)) < 0) return;
  uint32_t d1 = rd32(0x5ae02);
  uint32_t d0;
  if (int32_t(d1) > 32767) {
    d0 = 9999;
  } else {
    d0 = (800000u / (d1 & 0xffffu)) & 0xffffu;
    if (int16_t(uint16_t(d0)) < int16_t(rd16(0x5ade0)))
      wr16(0x5ade0, uint16_t(d0));
    if (int16_t(uint16_t(d0)) > int16_t(rd16(0x5abd8))) {
      wr16(0x5abd8, uint16_t(d0));
      wr16(0x5adde, uint16_t(d0));
    }
  }
  D(0) = d0;
  D(1) = d1;
  wr32(0x5adf2, rd32(0x5adea));
  wr32(0x5adf6, rd32(0x5adee));
}
NATIVE_ASM(0x399f6, refresh_airspeed_limits_399f6, true);

// $397b2: walk name buffers; always ends by setting $5aba4 = 1.
static void compare_names_397b2() {
  A(0) = 0x5aae3;
  A(1) = 0x5aac3;
  // dbf count = word at $2d04a + 2; dbf runs count+1 times
  int32_t count = int32_t(uint16_t(rd16(0x2d04a) + 2));
  D(1) = uint32_t(uint16_t(count));
  for (;;) {
    int8_t c = int8_t(rd8(A(1)));
    A(1) += 1;
    setb(0, uint8_t(c));
    if (c <= 0) break;
    int8_t o = int8_t(rd8(A(0)));
    A(0) += 1;
    if (c != o) break;
    if (count == 0) break;
    --count;
    D(1) = uint32_t(uint16_t(count));
  }
  wr8(0x5aba4, 1);
}
NATIVE_ASM(0x397b2, compare_names_397b2, true);

// $38baa: player-object readiness predicate → d0 = 0 or 1.
static void player_ready_pred_38baa() {
  A(0) = 0x5b47c;
  auto fail = []() { D(0) = 1; };
  auto ok = []() { D(0) = 0; };
  if (!(rd8(0x5b47c + 33) & 1)) { fail(); return; }
  if (!(rd8(0x5b47c + 3) & 0x80)) { fail(); return; }
  if (rd8(0x5ab40) == 3) {
    if ((rd8(0x5b47c + 4) & 0xc0) == 0) { fail(); return; }
  } else {
    // non-mode-3: require bit 2 of +4, then fall through to +110 test
    if (!(rd8(0x5b47c + 4) & 4)) { fail(); return; }
  }
  if (rd16(0x5b47c + 110) != 0) { fail(); return; }
  ok();
}
NATIVE_ASM(0x38baa, player_ready_pred_38baa, true);

// $4120a: merge steering bits into object byte a1@(101).
static void object_steer_flags_4120a() {
  uint8_t bits = 0;
  uint8_t mode = uint8_t(rd8(A(1) + 100) & 0x60);
  int16_t d3 = int16_t(lo(3));
  if (mode == 0x60) {
    if (d3 == 0) {
      bits = 0;
    } else if (d3 > 0) {
      if (d3 <= int16_t(rd16(A(1) + 88))) bits = 0;
      else {
        bits = 0x80;
        int16_t alt = int16_t(rd16(A(1) + 106));
        if (alt <= 14400) {
          if (alt > 80) bits = uint8_t(bits | 4);
        } else if (alt <= 28720) {
          bits = uint8_t(bits | 8);
        }
      }
    } else {  // d3 < 0
      if (d3 >= int16_t(rd16(A(1) + 88))) bits = 0;
      else {
        bits = 0x40;
        int16_t alt = int16_t(rd16(A(1) + 106));
        if (alt <= 14400) {
          if (alt > 80) bits = uint8_t(bits | 4);
        } else if (alt <= 28720) {
          bits = uint8_t(bits | 8);
        }
      }
    }
  } else if (rd8(A(1) + 100) & 0x80) {
    int16_t v = int16_t(rd16(A(1) + 106));
    bool take = false;
    if (v <= 14400) {
      if (v >= 400) take = true;
    } else if (v > 28400) {
      take = true;
    }
    if (take) {
      if (d3 == 0) bits = 0;
      else if (d3 > 0) bits = 8;
      else bits = 4;
    } else {
      bits = 0;
    }
  } else {
    bits = 0;
  }
  uint8_t d1 = uint8_t((rd8(A(1) + 101) & 3) | bits);
  setb(2, bits);
  setb(1, d1);
  wr8(A(1) + 101, d1);
}
NATIVE_ASM(0x4120a, object_steer_flags_4120a, true);

// ---- Render-adjacent pure math / object helpers (batch) --------------------------

// $388ee: copy a 5-word (+ long) parameter block from table $3ddc8 indexed by
// a1@(58) into a1@(44..56).
static void load_obj_params_388ee() {
  D(0) = 0;
  setb(0, rd8(A(1) + 58));
  setw(0, lo(0) + lo(0));
  A(4) = 0x3ddc8 + uint32_t(int16_t(rd16(0x3ddc8 + uint32_t(int16_t(lo(0))))));
  // movemw a4@+, d2-d6  — 5 words; then the fifth is treated as long at a1@(52)?
  // original: movemw a4@+,d2-d6 then movew d2..d5 to a1@(44..50), movel d6 to a1@(52)
  // movemw of 5 words: d2,d3,d4,d5,d6 — d6 only gets a word, but movel d6 stores long
  // with upper half whatever was in d6 before. After movemw, d6 high is 0 if cleared...
  // movemw sign-extends? On 68000 movem.w to data regs sign-extends to long.
  uint16_t w2 = rd16(A(4)); A(4) += 2;
  uint16_t w3 = rd16(A(4)); A(4) += 2;
  uint16_t w4 = rd16(A(4)); A(4) += 2;
  uint16_t w5 = rd16(A(4)); A(4) += 2;
  uint16_t w6 = rd16(A(4)); A(4) += 2;
  D(2) = uint32_t(int16_t(w2));
  D(3) = uint32_t(int16_t(w3));
  D(4) = uint32_t(int16_t(w4));
  D(5) = uint32_t(int16_t(w5));
  D(6) = uint32_t(int16_t(w6));
  wr16(A(1) + 44, lo(2));
  wr16(A(1) + 46, lo(3));
  wr16(A(1) + 48, lo(4));
  wr16(A(1) + 50, lo(5));
  wr32(A(1) + 52, D(6));
}
NATIVE_ASM(0x388ee, load_obj_params_388ee, true);

// $3ab16: perspective-style correction of object position at a1 using d5/d6/d7.
// If (d6>>8)==0, leave position alone; else adjust a1@(20) and a1@(28), clear @(24).
static void obj_persp_correct_3ab16() {
  D(0) = D(5);
  D(1) = D(6);
  D(2) = D(7);
  D(1) = uint32_t(int32_t(D(1)) >> 8);
  if (int32_t(D(1)) == 0) return;
  D(3) = rd32(A(1) + 24);
  D(3) = uint32_t(int32_t(D(3)) >> 8);
  setw(3, uint16_t(-int16_t(lo(3))));
  D(0) = uint32_t(int32_t(D(0)) << 4);
  D(2) = uint32_t(int32_t(D(2)) << 4);
  // divsw d1, d0 and d2 — signed 32/16 → 16 in low of d0/d2
  int16_t div = int16_t(lo(1));
  if (div != 0) {
    int32_t q0 = int32_t(D(0)) / int32_t(div);
    int32_t q2 = int32_t(D(2)) / int32_t(div);
    D(0) = uint32_t(int16_t(q0));
    D(2) = uint32_t(int16_t(q2));
  }
  D(0) = uint32_t(int32_t(int16_t(lo(0))) * int16_t(lo(3)));
  D(2) = uint32_t(int32_t(int16_t(lo(2))) * int16_t(lo(3)));
  D(0) = uint32_t(int32_t(D(0)) >> 4);
  D(2) = uint32_t(int32_t(D(2)) >> 4);
  wr32(A(1) + 20, uint32_t(int32_t(rd32(A(1) + 20)) + int32_t(D(0))));
  wr32(A(1) + 24, 0);
  wr32(A(1) + 28, uint32_t(int32_t(rd32(A(1) + 28)) + int32_t(D(2))));
}
NATIVE_ASM(0x3ab16, obj_persp_correct_3ab16, true);

// $3b462: C-arg index → scale entry at $5af6a + (arg<<6), same math as $3ab16
// on the long triple at entry+0/+4/+8 (using entry+12 as the source vector).
static void table_persp_correct_3b462() {
  const uint32_t sp = cpu().a[7];
  uint32_t idx = rd32(sp + 4);
  D(0) = idx;
  setw(0, uint16_t(int16_t(lo(0)) << 6));
  A(3) = 0x5af6a + uint32_t(int16_t(lo(0)));
  D(0) = rd32(A(3) + 12);
  D(1) = rd32(A(3) + 16);
  D(2) = rd32(A(3) + 20);
  D(1) = uint32_t(int32_t(D(1)) >> 8);
  if (int32_t(D(1)) == 0) {
    D(0) = 0;
    return;
  }
  D(3) = rd32(A(3) + 4);
  D(3) = uint32_t(int32_t(D(3)) >> 8);
  setw(3, uint16_t(-int16_t(lo(3))));
  D(0) = uint32_t(int32_t(D(0)) << 4);
  D(2) = uint32_t(int32_t(D(2)) << 4);
  int16_t div = int16_t(lo(1));
  if (div != 0) {
    D(0) = uint32_t(int16_t(int32_t(D(0)) / int32_t(div)));
    D(2) = uint32_t(int16_t(int32_t(D(2)) / int32_t(div)));
  }
  D(0) = uint32_t(int32_t(int16_t(lo(0))) * int16_t(lo(3)));
  D(2) = uint32_t(int32_t(int16_t(lo(2))) * int16_t(lo(3)));
  D(0) = uint32_t(int32_t(D(0)) >> 4);
  D(2) = uint32_t(int32_t(D(2)) >> 4);
  wr32(A(3) + 0, uint32_t(int32_t(rd32(A(3) + 0)) + int32_t(D(0))));
  wr32(A(3) + 4, 0);
  wr32(A(3) + 8, uint32_t(int32_t(rd32(A(3) + 8)) + int32_t(D(2))));
  // original then asrl #8 d0/d2 and forces d0=0 before rts
  D(0) = 0;
  D(2) = uint32_t(int32_t(D(2)) >> 8);
}
NATIVE_ASM(0x3b462, table_persp_correct_3b462, true);

// $42d8c: scale three consecutive word-triples at a1 by the word scale
// factors in $5ad36/$5ad38/$5ad3a (mulsw + asr #8), write back in place.
static void scale_matrix_rows_42d8c() {
  uint16_t s0 = rd16(0x5ad36);
  uint16_t s1 = rd16(0x5ad38);
  uint16_t s2 = rd16(0x5ad3a);
  D(0) = uint32_t(int16_t(s0));
  D(1) = uint32_t(int16_t(s1));
  D(2) = uint32_t(int16_t(s2));
  auto scale_row = [](uint32_t base, int16_t s) {
    for (int i = 0; i < 3; ++i) {
      int32_t v = int32_t(int16_t(rd16(base + uint32_t(i * 2)))) * int32_t(s);
      v >>= 8;
      wr16(base + uint32_t(i * 2), uint16_t(v));
    }
  };
  scale_row(A(1), int16_t(s0));
  A(1) += 6;
  scale_row(A(1), int16_t(s1));
  A(1) += 6;
  scale_row(A(1), int16_t(s2));
  // leftover d3-d5 = last row after scale
  D(3) = uint32_t(int16_t(rd16(A(1))));
  D(4) = uint32_t(int16_t(rd16(A(1) + 2)));
  D(5) = uint32_t(int16_t(rd16(A(1) + 4)));
}
NATIVE_ASM(0x42d8c, scale_matrix_rows_42d8c, true);

// ---- More pure render / object helpers ------------------------------------------

// $3cfc6: lookup byte from table at (ptr $2d03c)+12 indexed by $5ab9e; return 2*signext.
static void lookup_scaled_byte_3cfc6() {
  D(0) = 0;
  uint8_t d1 = rd8(0x5ab9e);
  setb(1, d1);
  if (d1 == 0x7e || d1 == 0x7f) return;
  setw(1, uint16_t(int16_t(int8_t(d1))));
  A(0) = rd32(0x2d03c);
  setb(0, rd8(A(0) + 12 + uint32_t(int16_t(lo(1)))));
  setw(0, uint16_t(int16_t(int8_t(lo(0)))));
  setw(0, lo(0) + lo(0));
}
NATIVE_ASM(0x3cfc6, lookup_scaled_byte_3cfc6, true);

// $432b8: map two angle indices (d0,d1) through table $60c84 into screen
// deltas relative to (319,179), store four words at (a1)+.
static void angles_to_screen_432b8() {
  setw(0, uint16_t(int16_t(lo(0)) << 3));
  setw(1, uint16_t(int16_t(lo(1)) << 3));
  A(0) = 0x60c84;
  uint16_t d2 = 319, d3 = 179;
  uint16_t d4 = d2, d5 = d3;
  d2 = uint16_t(d2 - rd16(A(0) + uint32_t(int16_t(lo(0)))));
  d3 = uint16_t(d3 - rd16(A(0) + uint32_t(int16_t(lo(0))) + 2));
  wr16(A(1), d2); A(1) += 2;
  wr16(A(1), d3); A(1) += 2;
  d4 = uint16_t(d4 - rd16(A(0) + uint32_t(int16_t(lo(1)))));
  d5 = uint16_t(d5 - rd16(A(0) + uint32_t(int16_t(lo(1))) + 2));
  wr16(A(1), d4); A(1) += 2;
  wr16(A(1), d5); A(1) += 2;
  D(2) = d2; D(3) = d3; D(4) = d4; D(5) = d5;
}
NATIVE_ASM(0x432b8, angles_to_screen_432b8, true);

// $4136a: sibling of $4120a — set pitch-ish bits 4/5 in a1@(101) from d3 vs a1@(86).
static void object_pitch_flags_4136a() {
  D(1) = 0;
  int16_t d3 = int16_t(lo(3));
  uint8_t d2;
  if (d3 < 0) {
    if (d3 >= int16_t(rd16(A(1) + 86))) d2 = 0;
    else d2 = 0x20;
  } else {
    if (d3 <= int16_t(rd16(A(1) + 86))) d2 = 0;
    else d2 = 0x10;
  }
  uint8_t d1 = rd8(A(1) + 101);
  // tstw d1: if the byte as word (high 0) is "negative" — only if we sign-extend?
  // original: moveb a1@(101),d1; tstw d1 — byte load into low of d1, high may be 0 from moveq
  // so tstw always non-negative if moveq #0 first... wait moveq was only for initial d1=0
  // after moveb, high byte of d1 is 0, so tstw always >= 0 → andib #3 path
  // Actually moveb doesn't clear high byte of d1 on 68000! So high byte remains 0 from moveq.
  // So always the andib #3 path.
  d1 = uint8_t((d1 & 3) | d2);
  setb(2, d2);
  setb(1, d1);
  wr8(A(1) + 101, d1);
}
NATIVE_ASM(0x4136a, object_pitch_flags_4136a, true);

// $36fb8: if selected object (offset $5acb8) is locked, clear selection.
static void clear_lock_if_gone_36fb8() {
  int16_t d0 = int16_t(rd16(0x5acb8));
  D(0) = uint32_t(uint16_t(d0));
  if (d0 < 0) return;
  A(1) = 0x5b47c + uint32_t(uint16_t(d0));
  if (!(rd8(A(1) + 1) & 0x40)) return;
  if (!(rd8(A(1) + 32) & 2)) return;
  wr16(0x5acb8, 0xffff);
  wr8(0x5ab60, 0);
  wr16(0x5ac32, 0xffff);
}
NATIVE_ASM(0x36fb8, clear_lock_if_gone_36fb8, true);

// ---- Buffer / object setup helpers ----------------------------------------------

// $44160: pick primary or alternate HUD buffer pair from $5a964.
static void select_hud_buffers_44160() {
  A(0) = 0x5a966;
  A(1) = 0x5a986;
  if (rd16(0x5a964) != 0) {
    A(0) += 16;
    A(1) += 20;
  }
  wr32(0x5a9ae, A(0));
  wr32(0x5a9b2, A(1));
}
NATIVE_ASM(0x44160, select_hud_buffers_44160, true);

// $4419e: zero four 40-byte blocks whose pointers are the four longs at a1@.
static void clear_four_blocks_4419e() {
  uint32_t p0 = rd32(A(1));
  uint32_t p1 = rd32(A(1) + 4);
  uint32_t p2 = rd32(A(1) + 8);
  uint32_t p3 = rd32(A(1) + 12);
  A(0) = p0; A(1) = p1; A(2) = p2; A(3) = p3;
  D(0) = 0;
  for (int r = 1; r <= 7; ++r) D(r) = 0;
  A(4) = 0; A(5) = 0;
  auto clear40 = [](uint32_t base) {
    // moveml d0-d7/a4-a5 stores 10 longs = 40 bytes
    for (int i = 0; i < 10; ++i) wr32(base + uint32_t(i * 4), 0);
  };
  clear40(p0);
  clear40(p1);
  clear40(p2);
  clear40(p3);
}
NATIVE_ASM(0x4419e, clear_four_blocks_4419e, true);

// $3000e: build world-space eye offsets from object slot in d7.
static void object_eye_offsets_3000e() {
  A(2) = 0x5b47c;
  setw(1, lo(7));
  setw(6, lo(7));
  setw(1, lo(1) & 0xff00);
  setw(1, lo(1) + lo(1));
  A(2) += uint32_t(int16_t(lo(1)));
  setw(6, lo(6) & 0xf);
  int32_t d3 = int32_t(rd32(A(2) + 20) & 0x000fffffu);
  d3 >>= int(lo(6) & 63);
  D(2) = uint32_t(int32_t(D(2)) << 8);
  D(2) = uint32_t(int32_t(D(2)) + d3);
  d3 = int32_t(rd32(A(2) + 28) & 0x000fffffu);
  d3 >>= int(lo(6) & 63);
  D(4) = uint32_t(int32_t(D(4)) << 8);
  D(4) = uint32_t(int32_t(D(4)) + d3);
  d3 = int32_t(rd32(A(2) + 24));
  D(1) = uint32_t(d3);
  d3 >>= int(lo(6) & 63);
  D(3) = uint32_t(d3);
  D(1) = uint32_t(int32_t(D(1)) + int32_t(rd32(0x5ad5e)));
  D(1) = uint32_t(int32_t(D(1)) >> int(lo(6) & 63));
  wr32(0x5ae34, D(1));
  wr8(0x5abb3, 1);
}
NATIVE_ASM(0x3000e, object_eye_offsets_3000e, true);

// $42682 / $42690 / $42696: toggle / force object timer word at a1@(80).
static void obj_timer_force_neg_42682() {
  if (rd16(A(1) + 80) != 0) {
    wr16(A(1) + 80, 0xffff);
    return;
  }
  wr16(A(1) + 80, 0xffff);
  wr16(A(1) + 84, uint16_t(-int16_t(rd16(A(1) + 84))));
}
static void obj_timer_clear_42690() {
  wr16(A(1) + 80, 0);
  wr16(A(1) + 84, uint16_t(-int16_t(rd16(A(1) + 84))));
}
static void obj_timer_set_neg_42696() {
  wr16(A(1) + 80, 0xffff);
  wr16(A(1) + 84, uint16_t(-int16_t(rd16(A(1) + 84))));
}
NATIVE_ASM(0x42682, obj_timer_force_neg_42682, true);
NATIVE_ASM(0x42690, obj_timer_clear_42690, true);
NATIVE_ASM(0x42696, obj_timer_set_neg_42696, true);

// ---- Sin/cos and object-frame helpers -------------------------------------------

// $42eba: angle in d4 (0..7199 units, full circle 7200) → (sin,cos)-like
// table lookups at $538e8 into d4/d5 with quadrant sign fixups.
static void sincos_table_42eba() {
  // 68000 (0,a0,dn.w) sign-extends the index. Match leftovers per quadrant.
  auto tbl = [](int16_t idx) -> uint16_t {
    return rd16(0x538e8u + uint32_t(int32_t(idx)));
  };
  setw(4, lo(4) + lo(4));  // addw d4,d4
  A(0) = 0x538e8;
  setw(7, lo(4));
  int16_t ang = int16_t(lo(4));
  if (ang < 1800) {
    // Q1: d4=sin, d6=1800-ang, d5=cos, d7=ang
    setw(4, tbl(ang));
    int16_t d6 = int16_t(1800 - int16_t(lo(7)));
    setw(6, uint16_t(d6));
    setw(5, tbl(d6));
  } else if (ang < 3600) {
    // Q2: d4=sin, d6=1800, d7=ang-1800, d5=-cos
    int16_t d6 = int16_t(3600 - ang);
    setw(6, uint16_t(d6));
    setw(4, tbl(d6));
    setw(6, 1800);
    setw(7, uint16_t(int16_t(lo(7)) - 1800));
    setw(5, tbl(int16_t(lo(7))));
    setw(5, uint16_t(-int16_t(lo(5))));
  } else if (ang < 5400) {
    // Q3: d4=-sin, d6=5400-ang, d5=-cos, d7=ang
    int16_t d6 = int16_t(ang - 3600);
    setw(4, tbl(d6));
    setw(4, uint16_t(-int16_t(lo(4))));
    d6 = int16_t(5400 - int16_t(lo(7)));
    setw(6, uint16_t(d6));
    setw(5, tbl(d6));
    setw(5, uint16_t(-int16_t(lo(5))));
  } else {
    // Q4: d4=-sin, d6=5400, d7=ang-5400, d5=cos
    int16_t d6 = int16_t(7200 - ang);
    setw(6, uint16_t(d6));
    setw(4, tbl(d6));
    setw(4, uint16_t(-int16_t(lo(4))));
    setw(6, 5400);
    setw(7, uint16_t(int16_t(lo(7)) - 5400));
    setw(5, tbl(int16_t(lo(7))));
  }
}
NATIVE_ASM(0x42eba, sincos_table_42eba, true);

// $2f11a / $2f124: set or clear bit 7 of player-object+124 depending on d7 flag.
static void player_flag_set_2f11a() {
  cpu().a[7] -= 12;
  wr32(cpu().a[7], D(4));
  wr32(cpu().a[7] + 4, D(7));
  wr32(cpu().a[7] + 8, A(1));
  setb(7, 1);
  A(1) = 0x5b47c + uint32_t(int16_t(rd16(0x5acae)));
  if (!(rd8(A(1) + 3) & 0x80)) {
    if (lob(7) == 0)
      wr8(A(1) + 124, uint8_t(rd8(A(1) + 124) & ~0x80));
    else
      wr8(A(1) + 124, uint8_t(rd8(A(1) + 124) | 0x80));
  }
  D(4) = rd32(cpu().a[7]);
  D(7) = rd32(cpu().a[7] + 4);
  A(1) = rd32(cpu().a[7] + 8);
  cpu().a[7] += 12;
}
static void player_flag_clear_2f124() {
  cpu().a[7] -= 12;
  wr32(cpu().a[7], D(4));
  wr32(cpu().a[7] + 4, D(7));
  wr32(cpu().a[7] + 8, A(1));
  setb(7, 0);
  A(1) = 0x5b47c + uint32_t(int16_t(rd16(0x5acae)));
  if (!(rd8(A(1) + 3) & 0x80)) {
    if (lob(7) == 0)
      wr8(A(1) + 124, uint8_t(rd8(A(1) + 124) & ~0x80));
    else
      wr8(A(1) + 124, uint8_t(rd8(A(1) + 124) | 0x80));
  }
  D(4) = rd32(cpu().a[7]);
  D(7) = rd32(cpu().a[7] + 4);
  A(1) = rd32(cpu().a[7] + 8);
  cpu().a[7] += 12;
}
NATIVE_ASM(0x2f11a, player_flag_set_2f11a, true);
NATIVE_ASM(0x2f124, player_flag_clear_2f124, true);

// $31b34: shift object a1@(6/8/10) by d1&15, subtract camera bias into d2/d4, leave d3.
static void obj_cam_delta_31b34() {
  cpu().a[7] -= 6;
  wr16(cpu().a[7], lo(5));
  wr16(cpu().a[7] + 2, lo(6));
  wr16(cpu().a[7] + 4, lo(7));
  int32_t d2 = int32_t(int16_t(rd16(A(1) + 6)));
  int32_t d4 = int32_t(int16_t(rd16(A(1) + 10)));
  setw(1, lo(1) & 15);
  int sh = int(lo(1) & 63);
  d2 <<= sh;
  d4 <<= sh;
  uint16_t w14 = rd16(A(1) + 14);
  int32_t d3 = int32_t(w14 >> 8);
  int32_t d5 = int32_t(w14 & 0xff);
  uint16_t d6 = uint16_t(rd16(0x5ac44) & 0xff);
  uint16_t d7 = uint16_t(rd16(0x5ac46) & 0xff);
  d3 = int32_t(uint16_t(uint16_t(d3) - d6));
  d5 = int32_t(uint16_t(uint16_t(d5) - d7));
  d3 = int32_t(uint32_t(d3) << 16) >> 2;  // swap then asr #2
  d5 = int32_t(uint32_t(d5) << 16) >> 2;
  d2 -= d3;
  d4 -= d5;
  d3 = int32_t(int16_t(rd16(A(1) + 8)));
  d3 <<= sh;
  D(2) = uint32_t(d2);
  D(3) = uint32_t(d3);
  D(4) = uint32_t(d4);
  setw(5, rd16(cpu().a[7]));
  setw(6, rd16(cpu().a[7] + 2));
  setw(7, rd16(cpu().a[7] + 4));
  cpu().a[7] += 6;
}
NATIVE_ASM(0x31b34, obj_cam_delta_31b34, true);

NATIVE_STATE(0x21e0c, st_mission_menu_wait);
NATIVE_STATE(0x21f8e, st_flight_log);
NATIVE_STATE(0x22026, st_22026);
NATIVE_STATE(0x222d6, st_222d6);
NATIVE_STATE(0x22354, st_22354);
NATIVE_STATE(0x22380, st_22380);
NATIVE_STATE(0x223ca, st_223ca);
NATIVE_STATE(0x223f6, st_223f6);
NATIVE_STATE(0x2253c, st_2253c);
NATIVE_STATE(0x22570, st_22570);
NATIVE_STATE(0x225b0, st_225b0);
NATIVE_STATE(0x2261a, st_2261a);
NATIVE_STATE(0x226fe, st_226fe);
NATIVE_STATE(0x2274c, st_2274c);
NATIVE_STATE(0x2277e, st_2277e);
NATIVE_STATE(0x227a4, st_227a4);
NATIVE_STATE(0x227d0, st_227d0);
NATIVE_STATE(0x22886, st_22886);
NATIVE_STATE(0x228b2, st_228b2);
NATIVE_STATE(0x228e2, st_228e2);
NATIVE_STATE(0x22a32, st_22a32);
NATIVE_STATE(0x22a56, st_22a56);
NATIVE_STATE(0x22a58, st_22a58);
NATIVE_STATE(0x22a9a, st_22a9a);
NATIVE_STATE(0x22ac8, st_22ac8);
NATIVE_STATE(0x22b04, st_22b04);
NATIVE_STATE(0x22b7c, st_22b7c);
NATIVE_STATE(0x22c0a, st_22c0a);
NATIVE_STATE(0x22c3e, st_22c3e);
NATIVE_STATE(0x22c76, st_22c76);
NATIVE_STATE(0x22d06, st_22d06);
NATIVE_STATE(0x22d60, st_22d60);
NATIVE_STATE(0x22430, st_22430);
NATIVE_STATE(0x2245a, st_2245a);
NATIVE_STATE(0x224ba, st_224ba);
NATIVE_STATE(0x22dc0, st_22dc0);
NATIVE_STATE(0x22e56, st_22e56);
NATIVE_STATE(0x22ee2, st_22ee2);
NATIVE_STATE(0x2353c, st_2353c);
NATIVE_STATE(0x23576, st_23576);
NATIVE_STATE(0x2359e, st_2359e);
NATIVE_STATE(0x235d0, st_235d0);
NATIVE_STATE(0x2362a, st_2362a);
NATIVE_STATE(0x23b7e, st_23b7e);
NATIVE_STATE(0x23ba8, st_23ba8);
NATIVE_STATE(0x231a4, st_231a4);
NATIVE_STATE(0x231d0, st_231d0);
NATIVE_STATE(0x236f6, st_236f6);
NATIVE_STATE(0x23712, st_23712);
NATIVE_STATE(0x237f2, st_237f2);
NATIVE_STATE(0x23808, st_23808);
NATIVE_STATE(0x23826, st_23826);
NATIVE_STATE(0x23890, st_23890);
NATIVE_STATE(0x238e0, st_238e0);
NATIVE_STATE(0x23988, st_23988);
NATIVE_STATE(0x239ca, st_239ca);
NATIVE_STATE(0x239f8, st_239f8);
NATIVE_STATE(0x23a3e, st_23a3e);
NATIVE_STATE(0x23a54, st_23a54);
NATIVE_STATE(0x23a8c, st_23a8c);
NATIVE_STATE(0x23ab0, st_23ab0);
NATIVE_STATE(0x23b2c, st_23b2c);
NATIVE_STATE(0x22f06, st_22f06);
NATIVE_STATE(0x231fc, st_231fc);

// $2346a: clear the message queue / text state.
static void clear_messages() {
  message_queue = 0;
  message_queue.at(1) = 0;
  message_y = 0x1b8;
  b_5aabe = 0;
  b_5aabb = 0;
  b_5aad8 = 0;
  b_5ab69 = 0;
}
NATIVE_C(0x2346a, clear_messages, true);

// $23c9c: per-frame sound-priority / ab88 countdown (called from game_frame).
static void tick_sound_priority() {
  int8_t n = int8_t(b_5ab88 - 1);
  b_5ab88 = n;
  if (n <= 0) {
    b_5ab88 = 8;
    b_5ab86 = -122;
  } else if (n == 4) {
    b_5ab86 = 6;
  } else if ((n & 3) == 2) {
    b_5ab86 = 4;
  } else if (n > 8) {
    b_5ab88 = 8;
    b_5ab86 = 0;
  }
}
NATIVE_C(0x23c9c, tick_sound_priority, true);

// $23c24: copy 16 longwords (32 palette entries) from `src` into the fade
// target buffer and request a fade (ab9c = 2).
static void load_fade_palette(uint32_t src) {
  uint32_t dst = fade_palette;  // pointer value at $5a958
  for (int i = 0; i < 16; ++i) {
    wr32(dst, rd32(src));
    src += 4;
    dst += 4;
  }
  b_5ab9c = 2;
}
NATIVE_C(0x23c24, load_fade_palette, true);

// $23c66: clear the fade-target palette and request a fade.
static void clear_fade_palette() {
  b_5ab9c = 2;
  uint32_t dst = fade_palette;
  for (int i = 0; i < 16; ++i) {
    wr32(dst, 0);
    dst += 4;
  }
}
NATIVE_C(0x23c66, clear_fade_palette, true);

// $23d08: record a game-event code when bit 8 of abc4 is set (in-flight
// systems enabled).  Used for landing / touchdown cues.
static void post_game_event(uint32_t code) {
  if (!(uint16_t(w_5abc4) & 0x100)) return;
  uint16_t c = uint16_t(code);
  w_5add8 = int16_t(c);
  w_5abc4 = int16_t(uint16_t(w_5abc4) | 1);
  c = uint16_t(c & 0xff00);
  if (c == 0x4000 || c == 0x4800)
    w_5abc6 = int16_t(uint16_t(w_5abc6) & 0xdfff);
}
NATIVE_C(0x23d08, post_game_event, true);

// $2d178: copy rank byte from the flight-log record into ab9f/aba0.
static void load_rank_from_log() {
  uint32_t cfg = uint32_t(config_buf);
  uint8_t rank = uint8_t(rd16(cfg + 2));
  b_5ab9f = int8_t(rank);
  b_5aba0 = int8_t(rank);
}
NATIVE_C(0x2d178, load_rank_from_log, true);

// $2d190: write ab9f back into the flight-log rank word.
static void save_rank_to_log() {
  uint32_t cfg = uint32_t(config_buf);
  wr16(cfg + 2, uint16_t(int16_t(int8_t(b_5ab9f))));
}
NATIVE_C(0x2d190, save_rank_to_log, true);

// $2d382: zero the main gameplay flag block (bytes aa88..aabd, words abb8..ac1e).
static void clear_gameplay_flags() {
  for (uint32_t a = 0x5aa88; a < 0x5aabd; ++a) wr8(a, 0);
  for (uint32_t a = 0x5abb8; a < 0x5ac20; a += 2) wr16(a, 0);
}
NATIVE_C(0x2d382, clear_gameplay_flags, true);

// $2d3b2: set the sticky-true flag block aa7c..aa87 to 1.
static void set_sticky_true_flags() {
  for (uint32_t a = 0x5aa7c; a < 0x5aa88; ++a) wr8(a, 1);
}
NATIVE_C(0x2d3b2, set_sticky_true_flags, true);

// $29d82: play the two "engine spool" samples when sound bit 2 is armed.
static void play_engine_spool(uint32_t vol) {
  if (!(rd8(0x5ae52) & 4)) return;
  call_c(0x29c84, {35, 0, vol});
  call_c(0x29c84, {36, 1, vol});
}
NATIVE_C(0x29d82, play_engine_spool, false);

// $234a8: bump per-scenario and global mission-complete counters in the
// flight log after a successful sortie.
static void count_mission_complete() {
  uint32_t cfg = uint32_t(config_buf);
  uint32_t slot = cfg + 18 + uint32_t(int32_t(int8_t(scenario)));
  save_rank_to_log();  // persist helper used by the original before the bumps
  wr8(cfg + 6, uint8_t(scenario));
  wr8(cfg + 7, rd8(slot));
  wr16(cfg + 0x38, uint16_t(rd16(cfg + 0x38) + 1));
  int8_t g = int8_t(b_5ab9f + 1);
  if (g > 3) g = 3;
  b_5ab9f = g;
  int8_t s = int8_t(rd8(slot) + 1);
  if (s > 3) s = 3;
  wr8(slot, uint8_t(s));
  b_5aba0 = b_5ab9f;
}
NATIVE_C(0x234a8, count_mission_complete, false);

// $2f024 / $2f02c: arm the "systems online" boost counters used by the
// in-flight draw path.  $2f024 also resets the view angle to 144.
static void arm_flight_systems_flags() {
  // All of these are brief "boost" counters decremented by game_frame.
  constexpr int8_t k = 3;
  cpu().d[4] = 3;  // assembly routine: leaves moveq #3,d4
  b_5ab2f = k; b_5ab2e = k;
  b_5ab31 = k; b_5ab32 = k;
  b_5ab36 = k; b_5ab37 = k; b_5ab38 = k; b_5ab39 = k;
  b_5ab33 = k; b_5ab35 = k;
  b_5ab3b = k; b_5ab3c = k; b_5ab3d = k;
  b_5ab34 = k;
  if (b_5aab8 == 0) {
    w_5abd0 = 0;
    wr32(0x5ac10, 0);
  }
}
static void arm_flight_systems() {
  w_5ac7c = 144;
  arm_flight_systems_flags();
}
NATIVE_ASM(0x2f02c, arm_flight_systems_flags, true);
NATIVE_C(0x2f024, arm_flight_systems, true);

// $292b4: mouse button state.  Bit 0 = LMB (via the original's BSS-as-CIA
// bug at $2a862 bit 6), bit 1 = RMB (POTGOR DATLY, active low).
static int32_t mouse_buttons() {
  int32_t d7 = 0;
  if (!(rd8(0x2a862) & 0x40)) d7 = 1;  // LMB pressed when bit 6 clear
  uint16_t potgor = rd16(0xdff016);
  if (!(potgor & (1 << 10))) d7 |= 2;  // RMB pressed when DATLY clear
  return d7;
}
NATIVE_C(0x292b4, mouse_buttons, true);

// $29c60: stop one Paula audio channel (clear the soft slot, notify driver).
static void stop_sound_channel(uint32_t ch) {
  wr32(0x65128 + (ch << 2), 0);
  call_c(0x6529c, {ch});
}
NATIVE_C(0x29c60, stop_sound_channel, false);

// $25e8c: on both mouse buttons held, set an abort/restart cue in ab68.
static void mouse_chord_check() {
  if (rd32(0x5b4ee) == 0) return;
  if (b_5aad0 == 0) return;
  if ((uint16_t(w_5aa78) & 3) != 3) return;
  if (rd16(0x5b47e) & 8)
    b_5ab68 = 119;
  else if (int8_t(rd8(0x5b4a7)) >= 120)
    b_5ab68 = 121;
  w_5aa78 = 0;
}
NATIVE_C(0x25e8c, mouse_chord_check, true);

// $28d4a: pull one raw keyboard event byte from keyboard.device (0 if none).
static int8_t fetch_key_byte() {
  uint32_t msg = os::GetMsg(rd32(0x2a580));
  if (msg == 0) return 0;
  uint32_t ie = rd32(0x2d074);
  int8_t code = int8_t(rd16(ie + 6));
  wr16(ie + 6, 0);
  os::SendIO(rd32(0x2a558));
  return code;
}
NATIVE_C(0x28d4a, fetch_key_byte, true);

// $22ce8: enable in-flight systems bit and refresh the view transform.
static void enable_inflight_systems() {
  sub_2d526();
  w_5abc4 = int16_t(uint16_t(w_5abc4) | 0x800);
  sub_36b88();
}
NATIVE_C(0x22ce8, enable_inflight_systems, false);

// $2f0b2: joystick fire pressed.
static void joy_fire_down() {
  // Assembly routine; d4.b holds the masked flag bytes like the original.
  auto set_d4b = [](uint8_t v) { cpu().d[4] = (cpu().d[4] & 0xFFFFFF00u) | v; };
  uint8_t busy = uint8_t(rd8(0x5b4f8) & 0x0f);
  set_d4b(busy);
  if (busy != 0) return;
  if (scenario == 125) {
    if (b_5aa92 < 0) wr8(0x5bc7e, uint8_t(rd8(0x5bc7e) | 8));
    return;
  }
  wr8(0x5ac92, rd8(0x5ac92) | 0x04);  // bset #2,$5ac92 (byte)
  uint8_t hi = uint8_t(rd8(0x5b4df) & 0xf0);
  set_d4b(hi);
  if (hi == 0) return;
  if (hi == 0x10)
    w_5abbe = int16_t(uint16_t(w_5abbe) | 8);
  else
    b_5aab2 = 1;
}
NATIVE_ASM(0x2f0b2, joy_fire_down, true);

// $2f108: joystick fire released.
static void joy_fire_up() {
  wr8(0x5ac92, rd8(0x5ac92) | 0x08);  // bset #3,$5ac92 (byte)
  w_5abbe = int16_t(uint16_t(w_5abbe) & ~8);
}
NATIVE_C(0x2f108, joy_fire_up, true);

// $2fa6c: start the "left" briefing/text stream.
static void start_text_stream_left() {
  b_5ab5d = 0;
  w_5aca2 = w_5aca4;
  // falls through into the shared stream pump at $2fa90 — keep via call
  call_c(0x2fa90);
}
NATIVE_C(0x2fa6c, start_text_stream_left, false);

// $2fa7e: start the "right" briefing/text stream.
static void start_text_stream_right() {
  b_5ab5d = 1;
  w_5aca2 = w_5aca6;
  call_c(0x2fa90);
}
NATIVE_C(0x2fa7e, start_text_stream_right, false);

// $28dae: next keyboard event as a signed rawkey, or -1 if none.
// Sticky-shift path ($2a5a6) returns the previously latched key.
static int16_t read_rawkey() {
  if (rd16(0x2a5a6) != 0) {
    wr16(0x2a5a6, 0);
    return int16_t(rd16(0x2d090));
  }
  int8_t raw = fetch_key_byte();
  if (raw == 0) return 0xFF;
  uint8_t code = uint8_t(raw) & 0x7f;
  if ((code & 0x70) == 0x70) return 0xFF;
  // Key-up events (bit 7 set) keep bit 7, key-down events return the code.
  if (uint8_t(raw) & 0x80) code = uint8_t(code + 0x80);
  return int16_t(int8_t(code));
}
NATIVE_C(0x28dae, read_rawkey, true);

// $28e5c: TR_GETSYSTIME into the game's timer IO, copy secs/micro out.
static void refresh_system_time() {
  wr8(0x2d054, 5);
  wr8(0x2d055, 0);
  wr32(0x2d056, 0);
  wr32(0x2d05a, 0);
  wr16(0x2d068, 10);
  os::DoIO(0x2d04c);
  wr32(0x5adea, rd32(0x2d06c));
  wr32(0x5adee, rd32(0x2d070));
}
NATIVE_C(0x28e5c, refresh_system_time, false);

// $2e268/$2e26c/$2e270: set pitch/vertical stick bits in ab26 (+ b4e1).
// Assembly routines: they leave d2 = bits and, when $5b4e1 is updated,
// the new byte in d1.b (registered with the register convention so the
// register results match exactly).
static void stick_v(uint8_t bits) {
  cpu().d[2] = bits;
  b_5ab26 = int8_t(bits);
  if (b_5aaa5 != 0 || b_5aaac != 0) {
    uint8_t v = uint8_t((rd8(0x5b4e1) & 0xcf) | bits);
    cpu().d[1] = (cpu().d[1] & 0xFFFFFF00u) | v;
    wr8(0x5b4e1, v);
  }
}
static void stick_v_down() { stick_v(0x20); }
static void stick_v_up()   { stick_v(0x10); }
static void stick_v_rel()  { stick_v(0x00); }
NATIVE_ASM(0x2e268, stick_v_down, true);
NATIVE_ASM(0x2e26c, stick_v_up, true);
NATIVE_ASM(0x2e270, stick_v_rel, true);

// $2e2b4/$2e2b8/$2e2bc: set roll/horizontal stick bits in ab28 (+ b4e1).
static void stick_h(uint8_t bits) {
  cpu().d[2] = bits;
  b_5ab28 = int8_t(bits);
  if (b_5aaa5 != 0 || b_5aaac != 0) {
    uint8_t v = uint8_t((rd8(0x5b4e1) & 0xf3) | bits);
    cpu().d[1] = (cpu().d[1] & 0xFFFFFF00u) | v;
    wr8(0x5b4e1, v);
  }
}
static void stick_h_right() { stick_h(0x08); }
static void stick_h_left()  { stick_h(0x04); }
static void stick_h_rel()   { stick_h(0x00); }
NATIVE_ASM(0x2e2b4, stick_h_right, true);
NATIVE_ASM(0x2e2b8, stick_h_left, true);
NATIVE_ASM(0x2e2bc, stick_h_rel, true);

// $2e22c/$2e230/$2e23a (+ fall-through from $2e234): low two bits of
// $5b4e1 (no aaa5/aaac gate; always written).  $2e234 also clears $5ab68.
static void stick_lo(uint8_t bits, bool clear_ab68 = false) {
  if (clear_ab68) wr8(0x5ab68, 0);
  D(2) = bits;
  uint8_t v = uint8_t((rd8(0x5b4e1) & 0xfc) | bits);
  setb(1, v);
  wr8(0x5b4e1, v);
}
static void stick_lo_1() { stick_lo(1); }
static void stick_lo_2() { stick_lo(2); }
static void stick_lo_0() { stick_lo(0); }
static void stick_lo_0_clear() { stick_lo(0, true); }
NATIVE_ASM(0x2e22c, stick_lo_1, true);
NATIVE_ASM(0x2e230, stick_lo_2, true);
NATIVE_ASM(0x2e23a, stick_lo_0, true);
NATIVE_ASM(0x2e234, stick_lo_0_clear, true);

// $2f098: mark a one-shot effect / message request.
static void request_effect_2f098() {
  wr8(0x5aad5, uint8_t(rd8(0x5aad5) | 0x80));
  wr16(0x5ad3a, 0x80);
  wr8(0x5ab35, 3);
}
NATIVE_ASM(0x2f098, request_effect_2f098, true);

// $2f216: empty stub in the original (just rts).
static void stub_2f216() {}
NATIVE_C(0x2f216, stub_2f216, true);

// $399ee: if *(a0) >= 0, decrement the byte (signed).
static void dec_if_nonneg_399ee() {
  int8_t v = int8_t(rd8(A(0)));
  if (v >= 0) wr8(A(0), uint8_t(v - 1));
}
NATIVE_ASM(0x399ee, dec_if_nonneg_399ee, true);

// $397a2: write a digit nibble in d1.b as ASCII to (a0)+.
// Values > 9 become 'A'.. (add 7 then '0').
static void put_digit_397a2() {
  uint16_t d1 = lo(1);
  if (int8_t(uint8_t(d1)) > 9) d1 = uint16_t(d1 + 7);
  d1 = uint16_t(d1 + 0x30);
  setw(1, d1);
  wr8(A(0), uint8_t(d1));
  A(0) = A(0) + 1;
}
NATIVE_ASM(0x397a2, put_digit_397a2, true);

// $2d3cc / $2d3e0 / $2d3f4 / $2d408 / $2d41a: world-origin / camera offset
// vectors (positive set at $5af36 and negated set at $5af2a).  Pure ASM:
// entries load d0/d1/d2 then fall into / call the store tail; leftover
// registers are the negated components.
static void store_world_origin_tail() {
  wr32(0x5af36, D(0));
  wr32(0x5af3a, D(1));
  wr32(0x5af3e, D(2));
  D(0) = uint32_t(-int32_t(D(0) & 0x003fffffu));
  D(1) = uint32_t(-int32_t(D(1)));
  D(2) = uint32_t(-int32_t(D(2) & 0x003fffffu));
  wr32(0x5af2a, D(0));
  wr32(0x5af2e, D(1));
  wr32(0x5af32, D(2));
}
static void world_origin_2d3cc() {
  D(0) = 0x10c00000;
  D(1) = 0x05000000;
  D(2) = 0x11400000;
}
static void world_origin_2d3e0() {
  D(0) = 0x10800000;
  D(1) = 0x03000000;
  D(2) = 0x10c00000;
  store_world_origin_tail();
}
static void world_origin_2d3f4() {
  D(0) = 0x10546800;
  D(1) = 0x00005000;
  D(2) = 0x10a36700;
  store_world_origin_tail();
}
static void world_origin_2d408() {
  D(0) = 0x0f248000;
  D(1) = 0x00005000;
  D(2) = 0x0fccf000;
  store_world_origin_tail();
}
NATIVE_ASM(0x2d3cc, world_origin_2d3cc, true);
NATIVE_ASM(0x2d41a, store_world_origin_tail, true);
NATIVE_ASM(0x2d3e0, world_origin_2d3e0, true);
NATIVE_ASM(0x2d3f4, world_origin_2d3f4, true);
NATIVE_ASM(0x2d408, world_origin_2d408, true);

// $2d4a6: transform (d3,d4,d5) by the 3x3 word matrix at a1+$92 and add the
// translation at a1+$14/$18/$1c; result in d0/d1/d2 (asr #4).
// Core body at $2d4b6: matrix at a2 (9 words), translation at a1+$14/$18/$1c.
static void xform_body(uint32_t a1, uint32_t mat) {
  int16_t v3 = int16_t(lo(3)), v4 = int16_t(lo(4)), v5 = int16_t(lo(5));
  int32_t m0 = int16_t(rd16(mat + 0)), m1 = int16_t(rd16(mat + 2)), m2 = int16_t(rd16(mat + 4));
  int32_t m3 = int16_t(rd16(mat + 6)), m4 = int16_t(rd16(mat + 8)), m5 = int16_t(rd16(mat + 10));
  int32_t m6 = int16_t(rd16(mat + 12)), m7 = int16_t(rd16(mat + 14)), m8 = int16_t(rd16(mat + 16));
  int32_t d6 = int32_t(v3) * m0;
  int32_t d0 = int32_t(v4) * m1;
  int32_t d7 = int32_t(v5) * m2;
  d0 = d0 + d6 + d7;
  d6 = int32_t(v3) * m3;
  int32_t d1 = int32_t(v4) * m4;
  d7 = int32_t(v5) * m5;
  d1 = d1 + d6 + d7;
  int32_t d3p = int32_t(v3) * m6;
  int32_t d2 = int32_t(v4) * m7;
  int32_t d5p = int32_t(v5) * m8;
  d2 = d2 + d3p + d5p;
  d0 >>= 4; d1 >>= 4; d2 >>= 4;
  d0 += int32_t(rd32(a1 + 0x14));
  d1 += int32_t(rd32(a1 + 0x18));
  d2 += int32_t(rd32(a1 + 0x1c));
  D(0) = uint32_t(d0);
  D(1) = uint32_t(d1);
  D(2) = uint32_t(d2);
  D(3) = uint32_t(d3p);
  D(5) = uint32_t(d5p);
  D(6) = uint32_t(d6);
  D(7) = uint32_t(d7);
}
// Entries save a1/a2 on the stack and restore them before rts, so leftover
// a1/a2 must match the caller's values (not the working pointers).
// $2d4a6: a1 = $5b47c, matrix at a1+$92
static void xform_setup_2d4a6() {
  uint32_t s_a1 = A(1), s_a2 = A(2);
  xform_body(0x5b47c, 0x5b47c + 0x92);
  A(1) = s_a1;
  A(2) = s_a2;
}
// $2d48e: a1 = $5b47c + w_$5abd6, matrix at a1+$92; restore a1/a2
static void xform_2d48e() {
  uint32_t s_a1 = A(1), s_a2 = A(2);
  uint32_t a1 = 0x5b47c + uint32_t(int16_t(rd16(0x5abd6)));
  xform_body(a1, a1 + 0x92);
  A(1) = s_a1;
  A(2) = s_a2;
}
// $2d4a0: matrix at a1+$92 using the caller's a1; restore a1/a2
static void xform_2d4a0() {
  uint32_t s_a1 = A(1), s_a2 = A(2);
  xform_body(s_a1, s_a1 + 0x92);
  A(1) = s_a1;
  A(2) = s_a2;
}
// $2d468: save a1/a2, save d3-d5.w, jsr $422d2, restore d3-d5.w,
// working a1=$5b47c+w_$5abd6, matrix at $5af06; restore a1/a2.
static void xform_2d468() {
  uint32_t s_a1 = A(1), s_a2 = A(2);
  uint16_t s3 = lo(3), s4 = lo(4), s5 = lo(5);
  Regs in{};
  for (int i = 0; i < 8; ++i) { in.d[i] = D(i); in.a[i] = A(i); }
  Regs out = call_asm(0x422d2, in);
  for (int i = 0; i < 8; ++i) { D(i) = out.d[i]; A(i) = out.a[i]; }
  setw(3, s3); setw(4, s4); setw(5, s5);
  uint32_t a1 = 0x5b47c + uint32_t(int16_t(rd16(0x5abd6)));
  xform_body(a1, 0x5af06);
  A(1) = s_a1;
  A(2) = s_a2;
}
// $2d452: moveq #0,d3/d4; movew #-5760,d5; bsr $2d4a6 (restores a1/a2);
// store d0-d2 to $5af4e.  a1/a2 and upper d4 left as on entry (d3/d4 full 0).
static void xform_2d452() {
  uint32_t s_a1 = A(1), s_a2 = A(2);
  D(3) = 0;
  D(4) = 0;
  setw(5, uint16_t(int16_t(-5760)));
  xform_body(0x5b47c, 0x5b47c + 0x92);
  wr32(0x5af4e, D(0));
  wr32(0x5af52, D(1));
  wr32(0x5af56, D(2));
  A(1) = s_a1;
  A(2) = s_a2;
}
NATIVE_ASM(0x2d4a6, xform_setup_2d4a6, true);
NATIVE_ASM(0x2d48e, xform_2d48e, true);
NATIVE_ASM(0x2d4a0, xform_2d4a0, true);
NATIVE_ASM(0x2d468, xform_2d468, true);
NATIVE_ASM(0x2d452, xform_2d452, true);

// $2d1a4: reset display / fade state for a fresh scenario.
static void reset_display_state() {
  wr16(0x65708 + 42, uint16_t(int16_t(-4)));
  b_5ab9c = 0;
  b_5aacd = 0;
  b_5ab98 = 15;
  b_5ab99 = 15;
  b_5aba5 = 1;
  b_5aacc = 1;
  b_5ab41 = 17;
}
NATIVE_C(0x2d1a4, reset_display_state, true);

// $2d50a: clear taxi/menu flags then fall into the flight-start reset at
// $2d526.  Shared tail stays original for now.
static void begin_flight_reset() {
  b_5aa7d = 0;
  b_5aa9e = 0;
  b_5aaad = 1;
  b_5aaac = 1;
  call_c(0x2d526);  // full reset body
}
NATIVE_C(0x2d50a, begin_flight_reset, false);

// $2d880: reset the player object record at $5b47c and a handful of global
// flight-control words.  Pure memory stores; a1 left pointing at $5b47c.
static void reset_player_object_2d880() {
  A(1) = 0x5b47c;
  uint32_t a1 = A(1);
  wr16(a1 + 0x6c, 0);   // 108
  wr16(a1 + 0x6e, 0);   // 110
  wr16(a1 + 0x78, 0);   // 120
  wr32(a1 + 0x3e, 0);   // 62
  wr32(a1 + 0x42, 0);   // 66
  wr32(a1 + 0x46, 0);   // 70
  wr32(a1 + 0x56, 0);   // 86
  wr16(a1 + 0x5a, 0);   // 90
  wr32(a1 + 0x50, 0);   // 80
  wr16(a1 + 0x54, 0);   // 84
  wr8(a1 + 0x65, 0);    // 101
  wr8(a1 + 0x2b, 9);    // 43
  wr16(a1 + 0, uint16_t(rd16(a1 + 0) & 0x7fff));
  wr32(0x5ae48, 0);
  wr32(0x5ae4c, 0);
  wr16(0x5add8, 0);
  wr16(0x5add6, 0);
  wr16(0x5addc, uint16_t(int16_t(-1)));
}
NATIVE_ASM(0x2d880, reset_player_object_2d880, true);

// $2d8e0: further player-object / mode flags for free-flight start.
// Calls $2f182 (still original) then patches flags on the $5b47c record.
static void setup_player_flight_2d8e0() {
  A(0) = 0x5b47c;
  uint32_t a0 = A(0);
  wr8(a0 + 0x21, uint8_t(rd8(a0 + 0x21) & ~1));  // bclr #0
  wr8(a0 + 0x63, 13);  // 99
  {
    Regs in{};
    for (int i = 0; i < 8; ++i) { in.d[i] = D(i); in.a[i] = A(i); }
    Regs out = call_asm(0x2f182, in);
    for (int i = 0; i < 8; ++i) { D(i) = out.d[i]; A(i) = out.a[i]; }
    a0 = A(0);  // whatever $2f182 left in a0 (original does not reload)
  }
  wr16(a0 + 0, 0x0148);  // 328
  wr16(a0 + 0, uint16_t(rd16(a0 + 0) | 0x1080));
  wr16(a0 + 0x7e, 0x1400);  // 126 = 5120
  wr8(0x5ab91, 0);
  wr8(0x5ab92, 0);
  wr8(0x5abab, 0);
  wr8(0x5aba9, 0);
  wr8(0x5ab81, 0);
  wr8(0x5ab67, 0);
  wr8(0x5abad, 0);
  wr16(0x5ae3a, 0x7fff);
  wr8(0x5aad0, 1);
  wr8(a0 + 0x71, 0xff);  // 113
  wr16(0x5acb8, uint16_t(int16_t(-1)));
  wr8(0x5ab60, 0);
  if (rd8(0x5aa90) != 0)
    wr8(0x5aa90, 4);
}
NATIVE_ASM(0x2d8e0, setup_player_flight_2d8e0, true);

// $29074: sample JOY1DAT and fire edge helpers for each axis.
static void sample_joystick() {
  uint16_t joy = rd16(0xdff00c);
  w_5ac48 = int16_t(joy);
  // Axis A: X1^X0 → 2e26c, Y1^Y0 → 2e268, else release 2e270
  if ((((joy & 0x200) >> 1) ^ (joy & 0x100)) != 0) {
    stick_v_up();
    b_5ab29 = 1;
  } else if ((((joy & 2) >> 1) ^ (joy & 1)) != 0) {
    stick_v_down();
    b_5ab29 = 1;
  } else if (b_5ab29 != 0) {
    stick_v_rel();
    b_5ab29 = 0;
  }
  // Axis B: X1 → 2e2b4, Y1 → 2e2b8, else release 2e2bc
  if (joy & 0x200) {
    stick_h_right();
    b_5ab2a = 1;
  } else if (joy & 2) {
    stick_h_left();
    b_5ab2a = 1;
  } else if (b_5ab2a != 0) {
    stick_h_rel();
    b_5ab2a = 0;
  }
}
NATIVE_C(0x29074, sample_joystick, true);

// $29006: drain gameport.device fire-button events, then sample the stick.
static void poll_gameport() {
  uint32_t msg = os::GetMsg(rd32(0x2d0b4));
  if (msg == 0) {
    b_5ab25 = 0;
  } else {
    uint32_t ie = rd32(0x2d092);
    int16_t code = int16_t(rd16(ie + 6));
    if (code == 104) joy_fire_down();
    else if (code == 232) joy_fire_up();
    wr16(ie + 6, 0);
    os::SendIO(rd32(0x2d096));
  }
  sample_joystick();
}
NATIVE_C(0x29006, poll_gameport, false);

// $2151c: read input devices and feed key presses to the key handler.
static void poll_input() {
  poll_gameport();                       // gameport (fire button) events
  int16_t mb = int16_t(mouse_buttons()); // mouse buttons
  if (w_5aa76 != mb) {
    w_5aa76 = mb;
    w_5aa78 = mb;
    mouse_chord_check();
  }
  int8_t mode = b_5ab43;
  if (mode == 1 || mode == 2) {
    while (w_5ac8e != 0) sub_2d984();
  } else if (mode == 3) {
    while ((w_5ac8e | w_5ac90) != 0) sub_2d984();
  }
  for (;;) {
    int8_t key = int8_t(read_rawkey());
    if (key == -1) break;
    sub_2dad0(uint32_t(uint8_t(key)));
  }
  if (mode == 0) {
    w_5ac8e = w_5ac92;
    w_5ac90 = w_5ac94;
    w_5ac92 = 0;
    w_5ac94 = 0;
  } else if (mode == 1) {
    w_5ac90 = w_5ac94;
    w_5ac94 = 0;
  }
}
NATIVE_C(0x2151c, poll_input, false);

// $215fe: stop all four sound channels.
static void stop_all_sounds() {
  for (int ch = 0; ch < 4; ++ch) stop_sound_channel(uint32_t(ch));
}
NATIVE_C(0x215fe, stop_all_sounds, false);

// $21630: open input devices, initialise controls and enter the boot state.
static void init_game() {
  sub_28e30();   // timer.device
  sub_28ce4();   // keyboard.device
  uint32_t input_task = os::FindTask(str_input_device);
  os::SetTaskPri(input_task, -128);
  sub_28ea4();   // gameport.device
  sub_397e8();
  sub_299d2();
  sub_2925c(uint32_t(-960), uint32_t(-960), 960, 960);   // mouse limits
  sub_29284(160, 100);                                     // mouse centre
  b_5ab43 = 0;
  reset_display_state();
  load_rank_from_log();
  game_state = st_boot_check_addr;
}
NATIVE_C(0x21630, init_game, false);

// $216c2: write `value` as `digits` hex digits ending at buf[digits-1];
// leading zeros (but the last digit) become spaces.
static void format_hex(uint32_t buf, uint32_t value, int8_t digits) {
  uint32_t p = buf + uint32_t(int32_t(digits));
  for (int8_t i = 0; i < digits; ++i) {
    uint8_t c = uint8_t((value & 15) + '0');
    if (c > '9') c = uint8_t(c + 7);
    wr8(p, c);
    --p;
    value >>= 4;
  }
  --digits;
  ++p;
  for (int8_t i = 0; i < digits && rd8(p) == '0'; ++i, ++p) wr8(p, ' ');
}
NATIVE_C(0x216c2, format_hex, true);

// $21750: per-frame state machine step (and flight time accounting).
static void run_game_state() {
  if (b_5ab90 >= 0 && scenario > 0) {
    if (b_5ab22 != 0 && b_5aa88 == 0 && b_5ab24 < 0 && l_5ac0c != 0) {
      // Accumulate flight time into the log (in seconds?).
      int32_t t = l_5adea - l_5ac0c - l_5ac00;
      if (l_5abfc != 0) t -= l_5adea - l_5abfc;
      t -= l_5ac08;
      if (l_5ac04 != 0) t -= l_5adea - l_5ac04;
      if (t < 0 || t >= 18000) {
        w_5ac96 = 63;
        stub_2f216();
      } else {
        wr32(config_buf + 8, rd32(config_buf + 8) + uint32_t(t));
        log_dirty = 1;
      }
      l_5ac0c = 0;
    }
    if (b_5aa90 >= 0) {
      if (b_5ab22 == -1) {
        b_5ab22 = 0;
        b_5ab23 = 0;
        game_state = st_restart_addr;
      } else if (b_5ab22 == 1) {
        b_5ab22 = 0;
        if (b_5aba4 != 0) b_5ab23 = 1;
        b_5aaa6 = 1;
        state_timer = 3;
        in_flight = 0;
        b_5ab99 = 0;
        game_state = st_21a9e_addr;
      } else if (b_5ab22 == 2) {
        b_5ab23 = 0;
        if (b_5ab24 < 0) {
          b_5aa90 = -16;
          b_5ab22 = 0;
          b_5aba5 = 1;
          b_5aaa6 = 1;
          state_timer = 6;
          game_state = 0x231a4;
        }
      }
    } else if (b_5ab22 == 3) {
      b_5ab23 = 0;
      if (b_5ab24 < 0) {
        b_5ab22 = 0;
        state_timer = w_5abb8;
        game_state = 0x231d0;
      }
    }
  }
  if (b_5ab24 >= 0) b_5ab24 = int8_t(b_5ab24 - 1);
  ++b_5aab9;
  state_timer = int16_t(state_timer - 1);
  call_c(game_state);
  b_5aa9b = 0;
}
NATIVE_C(0x21750, run_game_state, false);

// $2112c: one game frame: input, state machine, simulation and drawing.
static void game_frame() {
  int16_t frame = frame_counter;
  poll_input();
  run_game_state();
  tick_sound_priority();
  if (in_flight) {
    debug_stage = 8;
    sub_241f0();
    debug_stage = 16;
    sub_3a312();
    sub_2f596();
    debug_stage = 32;
    sub_3a310();
    sub_4217c();
    sub_2f4a6();
    sub_39a54();
    sub_243fa();
    sub_2f7b8();
    debug_stage = 96;
    sub_39b06();
    sub_39dd0();
    sub_42f48();
    debug_stage = 104;
    if (b_5ab93 == 0 || b_5aaa8 != 0) sub_3f284();
    debug_stage = 112;
    sub_43306();
    if (l_5ad5e > -0x8000000) {
      debug_stage = 120;
      start_text_stream_left();
      debug_stage = 128;
      start_text_stream_right();
      debug_stage = 144;
      sub_3c1bc();
      debug_stage = 160;
      if (b_5aaa5 != 0) {
        sub_272e4();
      } else if (sub_3add8() != 0) {
        debug_stage = 164;
        sub_272e4();
        debug_stage = 168;
        sub_2fc14();
      } else {
        debug_stage = 172;
        sub_2fc14();
        debug_stage = 176;
        sub_272e4();
      }
    }
    debug_stage = 192;
    sub_23d54();
    sub_24aa8();
    debug_stage = 208;
    if (b_5aa7d == 0 || b_5ab2e != 0) {
      uint32_t plane = uint32_t(int32_t(int16_t(w_5abd4))) << 9;
      uint8_t c = rd8(0x5b47c + plane + 98);
      if (c != '0' || b_5ab2e != 0) {
        sub_45368();
        sub_455ba();
        sub_45e2a();
        sub_47ec0();
        sub_45d2e();
        sub_45b7c();
        int8_t boost = b_5ab2f;
        if (boost > 0 || (frame & 3) > 1) {
          sub_46aba();
          sub_46b50();
        }
        if (b_5ab2f > 0 || (frame & 3) < 2) sub_46d7c();
        if (b_5ab2f > 0 || (frame & 3) < 2 || l_5ad5e > -32768) sub_46c1e();
        if (b_5ab2f > 0 || (frame & 7) < 2) sub_46d2e();
        if (b_5ab2f > 0 || (frame & 15) < 2) sub_4551c();
        debug_stage = 416;
        sub_44c42();
        sub_474ac();
        sub_45604();
        sub_46dd6();
        sub_46e64();
        sub_466d0();
        sub_45938();
        sub_46668();
        sub_46ef2();
        sub_45760();
        if (b_5ab2f >= 0) b_5ab2f = int8_t(b_5ab2f - 1);
        debug_stage = 464;
      }
    } else if (b_5aa7d != 0) {
      b_5aad1 = 1;
      sub_46b50();
      sub_46c1e();
      sub_46aba();
      if (scenario == 2) sub_46ef2();
    }
    debug_stage = 468;
    sub_2439a();
    sub_46b4e();
    sub_3fd48();
    sub_3987e();
    if ((frame & 7) == 7) sub_399f6();
    if ((frame & 15) == 4) os::FreeSprite(0);
    if ((frame & 31) == 8) sub_4418a();
    else if (b_5aba6 == 0 && (frame & 31) == 16) arm_flight_systems_flags();
    if (b_5aaa6 == 0) frame_counter = int16_t(frame_counter + 1);
  } else {
    sub_24aa8();
    os::FreeSprite(0);
  }
  sub_3fba6();
  if (b_5aaaa != 0 && in_flight) {
    debug_stage = 528;
    sub_440a4();
    debug_stage = 536;
    sub_4677a();
  }
  debug_stage = 544;
  sub_478f2();
}
// Note: $2f7b8 (called from here) stores a flag at -44(a6), i.e. in this
// function's stack area, and $3127c later reads it back after its movem has
// overwritten it with the saved a4. This only works out like the original
// because invoke_native reproduces the link/movem frame and call_c does not
// restore registers.
NATIVE_C(0x2112c, game_frame, false);

// ---- states ---------------------------------------------------------------------

// $2196a: first state: check the picture checksums (shows an error code if
// the files were modified), then go to the title/menu.
static void st_boot_check() {
  sub_2d1e6();
  uint32_t dst = fade_palette;
  for (int i = 0; i < 32; ++i) wr16(dst + uint32_t(i) * 2, rd16(0x3a054 + uint32_t(i) * 2));
  scenario = 0;
  b_5aaa5 = -1;
  b_5ab4f = -1;
  state_timer = 3;
  game_state = st_2359e_addr;
  uint32_t msg = 0x5433c + 21;  // "...X" error digit position
  uint32_t bad = 0;
  if (uint16_t(splash_checksum) != 0xC560) { wr8(msg, '1'); bad = uint16_t(splash_checksum); }
  if (uint16_t(cockpit_checksum) != 0x7E70) { wr8(msg, rd8(msg) | '2'); bad = uint16_t(cockpit_checksum); }
  if (uint16_t(front_checksum) != 0x4DE8) { wr8(msg, rd8(msg) | '4'); bad = uint16_t(front_checksum); }
  if (bad != 0) {
    format_hex(msg + 1, bad, 4);
    game_state = st_2353c_addr;
  } else {
    sub_29cee(50);
  }
}

static void st_restart() {
  sub_2d1e6();
  // The original also clears $5aba4 here (moveb d0,$5aba4 at $21a80); that
  // store is NOP'd by the codewheel bypass (Machine::boot), which keeps the
  // protection-passed flag set. We emulate the patched program.
  scenario = 0;
  b_5ab43 = 0;
  game_state = st_mission_menu_addr;
}

static void st_21a9e() {
  if (state_timer < 0 && b_5ab98 == b_5ab99) {
    state_timer = 2;
    game_state = st_21acc_addr;
  }
}

static void st_21acc() {
  if (state_timer < 0) {
    in_flight = 1;
    game_state = st_21aea_addr;
  }
}

static void st_21aea() {
  stop_all_sounds();
  sub_2d1e6();
  if (scenario == 2) scenario = 125;
  if (b_5ab43 == 3) {
    b_5aaa6 = 1;
    state_timer = 2;
    b_5ab99 = 0;
    scenario = 127;
    game_state = st_21b5c_addr;
  } else {
    b_5ab43 = 0;
    load_fade_palette(0x39fd4);
    game_state = st_mission_menu_wait_addr;
  }
}

// $21bfc: set up the demo flight.
static void start_demo() {
  b_5aba0 = b_5ab9f;
  b_5ab9f = 0;
  b_5ab40 = 4;
  b_5ab40 = 3;
  b_5aba6 = 0;
  b_5aaa5 = 0;
  b_5aa88 = 1;
  sub_3cf0e();
  scenario = 3;
  begin_flight_reset();
  clear_messages();
  b_5aba5 = 0;
  in_flight = 1;
  w_5ac7e = 0;
  w_5ac80 = 0;
  arm_flight_systems();
  b_5ab50 = -1;
  state_timer = 1;
}
NATIVE_C(0x21bfc, start_demo, false);

static void st_21b5c() {
  if (state_timer < 0) {
    start_demo();
    b_5ab43 = 3;
    b_5aa7d = 0;
    state_timer = 2;
    b_5ab99 = 15;
    b_5ab98 = 0;
    game_state = st_21ba4_addr;
  } else {
    sub_4492a();
  }
}

static void st_21ba4() {
  if (state_timer < 0) {
    in_flight = 0;
    if (b_5ab98 == b_5ab99) {
      state_timer = 2;
      game_state = st_21bd8_addr;
    }
  }
}

static void st_21bd8() {
  if (state_timer < 0) {
    b_5aaa6 = 0;
    in_flight = 1;
    game_state = st_22d60_addr;
  }
}

static void st_21cc8() {
  if (state_timer < 0) {
    if (b_5ab23 == 0) {
      in_flight = 0;
      message_queue = 73;
      game_state = st_21d0e_addr;
    } else {
      in_flight = 1;
      clear_messages();
      game_state = st_22d60_addr;
    }
  }
}

static void st_21d0e() {
  if (b_5aad8 < 0 || b_5aa9b != 0) {
    in_flight = 1;
    clear_messages();
    game_state = st_22d60_addr;
  }
}

// $21d38: show the mission selection menu.
static void st_mission_menu() {
  music_volume2 = 0x1f0000;
  if ((sound_flags & 0x80) && title_flag_5ab47 == 0) music_volume = 0x1f0000;
  sub_29cee(15);
  title_flag_5ab47 = 1;
  sub_4492a();
  clear_messages();
  sub_208e2(0xc000);  // delay
  b_5aba5 = 0;
  load_fade_palette(0x39fd4);
  message_y = 480;
  static const int16_t menu[] = {6, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 0};
  for (int i = 0; i < 12; ++i) message_queue.at(i) = menu[i];
  game_state = st_mission_menu_wait_addr;
}

// $21e0c: wait for a selection in the mission menu.
static void st_mission_menu_wait() {
  int i = 0;
  int8_t sel = scenario;
  if (b_5ab22 != 0) {
    b_5ab22 = 0;
    if (w_5aa6a != 6) {
      clear_messages();
      game_state = st_mission_menu_addr;
    }
    return;
  }
  if (scenario > 0) {
    music_volume2 = 0;
    clear_messages();
    sub_4492a();
    if (sel == 127) message_queue.at(i++) = 101;
    else if (sel == 1) message_queue.at(i++) = 102;
    else if (sel == 2) message_queue.at(i++) = 103;
    else if (sel == 125) message_queue.at(i++) = 104;
    else if (sel == 9) message_queue.at(i++) = 105;
    else if (b_5aa8a != 0) {
      message_queue.at(i++) = 107;
      b_5aa8a = 0;
    }
    message_queue.at(i) = 0;
    state_timer = (b_5ab23 == 0 && (sound_flags & 0x80)) ? 210 : 150;
    game_state = st_22026_addr;
    return;
  }
  if (scenario < 0) {
    clear_messages();
    sub_4492a();
    if (scenario == -1) {
      game_state = st_222d6_addr;
    } else {
      sub_4492a();
      sub_393f6();
      message_queue.at(i++) = 87;
      message_queue.at(i) = 0;
      game_state = st_flight_log_addr;
    }
    scenario = 0;
    return;
  }
  b_5ab22 = 0;
}

// $21f8e: flight log screen (1: save to disk, SHIFT-2: reset).
static void st_flight_log() {
  if (b_5aa8a != 0) {
    if (b_5aa8a == 1) {
      if (log_dirty != 0 && disk_state == 0) {
        sub_28592();   // save_flight_log
        b_5aacd = -1;
        if (rd16(config_buf + 4) <= 1) game_state = st_2362a_addr;
      }
    } else if (b_5aa8a == 2) {
      sub_2855e();     // reset_flight_log
      clear_messages();
      sub_4492a();
      sub_393f6();
      message_queue = 87;
    }
    b_5aa8a = 0;
  } else if (b_5ab22 != 0) {
    game_state = st_mission_menu_addr;
  }
  scenario = 0;
}

// $22026: after a positive mission selection, set up the scenario and hand
// off to the in-flight state chain.  Runs once when state_timer expires.
static void st_22026() {
  int16_t sel = int16_t(int8_t(scenario));
  if (state_timer >= 0) return;

  b_5aacf = 0;  // title_flag_5ab47
  stop_sound_channel(0);
  stop_sound_channel(1);
  music_volume = 0x3f0000;

  if (sel >= 3 && sel <= 8) {
    play_engine_spool(40);
  } else if (rd8(0x5ae53) & 0x10) {
    sub_29fa2(8);
  } else {
    sub_29e4e(8, 0x300);
  }

  w_5ac7c = 179;
  clear_messages();
  state_timer = 4;
  b_5ab8f = 3;
  b_5ab90 = 2;
  sub_3cf0e();
  b_5aaa6 = 0;
  in_flight = 1;
  b_5ab2e = 3;
  b_5ab99 = b_5ab98;
  clear_fade_palette();
  game_state = st_2253c_addr;
  b_5ab40 = 3;
  b_5ab41 = 17;

  // Switch on the selected scenario.
  if (sel == 127) {
    b_5aaa6 = 1;
    game_state = st_21b5c_addr;
  } else if (sel == 1) {
    world_origin_2d3e0();
    b_5ab8f = 5;
    game_state = st_22354_addr;
  } else if (sel == 2) {
    w_5ac7c = 167;
    b_5aaac = -1;
    sub_3dc7c();
    b_5aaa5 = 0;
    b_5aa7d = 1;
    b_5aba6 = 0;
    b_5aaad = 1;
    w_5abd4 = 4;
    w_5abd6 = 2048;
    b_5abaa = 3;
    w_5bc7e = int16_t(uint16_t(w_5bc7e) | 0x100);
    game_state = st_223ca_addr;
  } else if (sel == 3) {
    b_5ab40 = 0;
  } else if (sel == 125) {
    b_5ab8f = 1;
    w_5ac7c = 167;
    b_5aaac = -1;
    b_5aaae = 3;
    b_5ab40 = 2;
    enable_inflight_systems();
    sub_3dc86();
    world_origin_2d408();
    sub_3cf0e();
    b_5aaa5 = 0;
    game_state = st_223ca_addr;
  } else if (sel == 9) {
    b_5ab40 = 3;
    b_5aba6 = 0;
    b_5aaa5 = 0;
    sub_3cf0e();
    begin_flight_reset();
    clear_messages();
    b_5aba5 = 1;
    in_flight = 1;
    w_5ac7e = 0;
    w_5ac80 = 0;
    arm_flight_systems();
    b_5ab50 = -1;
    b_5aa7d = 0;
    game_state = st_22354_addr;
  }

  if (b_5ab40 == 3) b_5ab41 = 17;
  sub_2f7b8();
}

// $222d6: build the selectable-missions list from the flight log and return
// to the mission-menu wait state.
static void st_222d6() {
  sub_4492a();
  uint32_t q = message_queue.ptr();
  wr16(q, 64);  // header message
  q += 2;
  uint32_t src = 0x53ffc;  // mission-name id table (word per mission)
  for (int i = 3; i < 9; ++i) {
    // config_buf + 0x12 is the per-mission unlock flag array
    if (rd8(config_buf + 0x12 + uint32_t(i)) != 0) {
      wr16(q, rd16(src));
      q += 2;
      src += 2;
    }
    // locked slots do not consume a name-table entry (original behaviour)
  }
  wr16(q, 0x806e);  // "return" entry
  q += 2;
  wr16(q, 0);
  game_state = st_mission_menu_wait_addr;
}

// $22354 / $22380: free-flight intro — wait for key release, then arm flight.
static void st_22354() {
  if (state_timer < 0) {
    in_flight = 0;
    b_5ab99 = 15;
    b_5ab98 = 0;
    game_state = st_22380_addr;
  }
}
static void st_22380() {
  if (b_5ab98 == b_5ab99) {
    in_flight = 1;
    b_5ab50 = -1;
    b_5aba6 = 4;
    b_5aba5 = 1;
    state_timer = 5;
    game_state = st_227d0_addr;
  }
}

// $223ca / $223f6: combat-mission intro (scenarios 2 / 125).
static void st_223ca() {
  if (state_timer < 0) {
    in_flight = 0;
    b_5ab99 = 15;
    b_5ab98 = 0;
    game_state = st_223f6_addr;
  }
}
static void st_223f6() {
  if (b_5ab98 == b_5ab99) {
    in_flight = 1;
    b_5ab50 = -1;
    b_5aba5 = 1;
    state_timer = 5;
    game_state = st_227d0_addr;
  }
}

// $2253c / $22570: default post-menu wait (most scenarios).
static void st_2253c() {
  b_5aba5 = 1;
  if (state_timer < 0) {
    in_flight = 0;
    b_5ab99 = 15;
    b_5ab98 = 0;
    game_state = st_22570_addr;
  }
}
static void st_22570() {
  if (b_5ab98 == b_5ab99) {
    in_flight = 1;
    b_5ab50 = -1;
    b_5aaa5 = 1;
    b_5ab4f = 1;
    state_timer = 9;
    game_state = st_225b0_addr;
  }
}

// $225b0: after key release, either abort on fire/escape or advance.
static void st_225b0() {
  b_5aa8e = int8_t(uint8_t(b_5aa8e) | uint8_t(b_5aa9b));
  if (b_5ab23 == 0 && b_5aa9b != 0) {
    in_flight = 1;
    clear_messages();
    state_timer = -1;
    game_state = st_2274c_addr;
    return;
  }
  if (state_timer < 0) {
    b_5ab4f = 0;
    state_timer = 1;
    game_state = st_2261a_addr;
  }
}

// $2261a: after the post-menu wait, either abort on fire or show the
// mission-unlock / briefing text for scenarios 3–8.
static void st_2261a() {
  b_5aaa6 = 0;
  b_5aa8e = int8_t(uint8_t(b_5aa8e) | uint8_t(b_5aa9b));
  if (b_5ab23 == 0 && b_5aa9b != 0) {
    in_flight = 1;
    clear_messages();
    state_timer = -1;
    game_state = st_2274c_addr;
    return;
  }
  if (state_timer < 0) {
    in_flight = 0;
    int16_t sel = int16_t(int8_t(scenario));
    if (sel >= 3 && sel <= 8) {
      message_queue = 91;
      uint8_t unlocked = rd8(config_buf + 0x12 + uint32_t(sel));
      if (unlocked == 0) {
        sub_39510(uint32_t(sel), 1, 0);
      } else {
        sub_39510(uint32_t(sel), 1, 1);
      }
    }
    game_state = st_226fe_addr;
  }
}

// $226fe: wait for message / input, then enter the pre-flight fade.
static void st_226fe() {
  if (b_5aad8 < 0 || b_5aa9b != 0) {
    b_5aa8e = int8_t(uint8_t(b_5aa8e) | uint8_t(b_5aa9b));
    in_flight = 1;
    clear_messages();
    state_timer = -1;
    b_5ab2e = 3;
    game_state = st_2274c_addr;
  }
}

// $2274c: short timer, then enable radar / view flags.
static void st_2274c() {
  if (state_timer < 0) {
    b_5aaa5 = 1;
    b_5ab4f = 1;
    state_timer = 1;
    sub_3db54();
    game_state = st_2277e_addr;
  }
}

// $2277e: set view mode 2 and fire-button latch.
static void st_2277e() {
  if (state_timer < 0) {
    b_5aba6 = 2;
    b_5aaad = 1;
    game_state = st_227a4_addr;
  }
}

// $227a4: wait until view mode reaches 3, then clear latch and continue.
static void st_227a4() {
  if (b_5aba6 == 3) {
    b_5aaad = 0;
    b_5ab4f = 0;
    state_timer = 2;
    game_state = st_227d0_addr;
  }
}

// $227d0: final pre-flight gate — either jump to codewheel path or show
// mission briefing messages and hand off to $228e2 (not yet translated).
static void st_227d0() {
  if (state_timer >= 0) return;
  in_flight = 0;
  if (b_5ab23 != 0) {
    b_5aba4 = 1;
    b_5aba5 = 2;
    state_timer = 3;
    game_state = st_228e2_addr;
    return;
  }
  // Build a short message queue from the scenario id (missions 3–8).
  uint32_t q = message_queue.ptr();
  int16_t sel = int16_t(int8_t(scenario));
  if (b_5aa8e == 0 && sel >= 3 && sel <= 8) {
    if (sel == 3) {
      wr16(q, 95);
      q += 2;
    } else {
      wr16(q, 96);
      q += 2;
    }
  }
  b_5aa8e = 0;
  wr16(q, 71);
  q += 2;
  wr16(q, 0);
  sub_396e2();
  game_state = st_22886_addr;
}


// $22886: after briefing message, advance message-queue index and continue.
static void st_22886() {
  if (int8_t(b_5aad8) - 1 == 0) {
    message_queue = 4;
    b_5aabb = 0;
    b_5aad8 = 3;
    game_state = st_228b2_addr;
  }
}

// $228b2: if codewheel path active, clear messages and enter $228e2.
static void st_228b2() {
  if (b_5aba4 != 0) {
    if (b_5aa7d != 0) b_5aba5 = 2;
    clear_messages();
    state_timer = 3;
    game_state = st_228e2_addr;
  }
}

// $228e2: codewheel / mission-complete gate; dispatch by scenario.
static void st_228e2() {
  if (state_timer >= 0) return;
  int16_t sel = int16_t(int8_t(scenario));
  if (b_5aba4 > 0) {
    game_state = st_22a58_addr;
    if (sel >= 3 && sel <= 8) {
      message_queue = 93;
      sub_39510(uint32_t(sel), 1, 3);
      // config_buf+0x36 = missions-completed counter
      wr16(config_buf + 0x36, uint16_t(rd16(config_buf + 0x36) + 1));
      log_dirty = 1;
    } else if (sel == 1) {
      game_state = st_22ac8_addr;
    } else if (sel == 2 || sel == 125) {
      game_state = st_22430_addr;
    } else if (sel == 9) {
      in_flight = 1;
      state_timer = 3;
      game_state = st_21cc8_addr;
    }
    return;
  }
  // b_5aba4 <= 0: retry / decrement attempt counter
  if (b_5aba4 < 0) {
    b_5aba4 = 0;
    in_flight = 1;
    clear_messages();
    state_timer = 3;
    b_5ab90 = int8_t(b_5ab90 - 1);
    if (b_5ab90 >= 0)
      game_state = st_227d0_addr;
    else
      game_state = st_22a32_addr;
  }
}

// $22a32 / $22a56: show "failed" message then idle.
static void st_22a32() {
  if (state_timer < 0) {
    in_flight = 0;
    message_queue = 67;
    game_state = st_22a56_addr;
  }
}
static void st_22a56() {
  // idle sink after failure message
}

// $22a58: wait for fire (aa9b==2) or message timer, then advance.
static void st_22a58() {
  if (b_5aa9b == 2) {
    st_22d06();
    return;
  }
  if (b_5aad8 < 0) {
    in_flight = 1;
    clear_messages();
    b_5ab4f = 1;
    state_timer = 3;
    game_state = st_22a9a_addr;
  }
}

static void st_22a9a() {
  if (b_5aa9b == 2) {
    st_22d06();
    return;
  }
  if (state_timer < 0) {
    b_5aaad = 1;
    game_state = st_22ac8_addr;
  }
}

static void st_22ac8() {
  if (b_5aa9b == 2) {
    st_22d06();
    return;
  }
  if (b_5aba6 == 4) {
    b_5aaad = 0;
    b_5ab4f = 0;
    state_timer = 2;
    game_state = st_22b04_addr;
  }
}

static void st_22b04() {
  if (b_5aa9b == 2) {
    st_22d06();
    return;
  }
  if (state_timer < 0) {
    in_flight = 0;
    if (scenario > 1) b_5aaad = 1;
    if (scenario == 1) {
      message_queue = 7;
      b_5aba6 = 5;
      b_5ab4f = 1;
    }
    b_5aba5 = 0;
    b_5aabb = 0;
    game_state = st_22b7c_addr;
  }
}

static void st_22b7c() {
  if (b_5aa9b == 2) {
    st_22d06();
    return;
  }
  if (b_5aaad != 0) {
    b_5aaad = 0;
    clear_messages();
    b_5aaa5 = 0;
    b_5ab2e = 3;
    in_flight = 1;
    state_timer = 2;
    if (b_5ab40 != 3) {
      game_state = st_22c0a_addr;
      b_5aa89 = 1;
      b_5ab4f = 0;
      b_5aba6 = 6;
    } else {
      game_state = st_22c76_addr;
    }
  } else if (int8_t(b_5aad8) - 1 == 0) {
    in_flight = 1;
  }
}

static void st_22c0a() {
  b_5aaad = 0;
  if (state_timer < 0) {
    in_flight = 0;
    message_queue = 75;
    b_5aabb = 0;
    game_state = st_22c3e_addr;
  }
}

static void st_22c3e() {
  if (b_5aa89 == 0) {
    state_timer = 2;
    in_flight = 1;
    clear_messages();
    b_5ab4f = 1;
    b_5aba5 = 1;
    game_state = st_22c76_addr;
  }
}

static void st_22c76() {
  if (state_timer < 0) {
    b_5aaad = 1;
    enable_inflight_systems();
    sub_2d452();
    b_5aaae = 0;
    b_5aba6 = 6;
    b_5aba5 = 0;
    int8_t sel = scenario;
    if (sel >= 3 && sel <= 8) {
      if (rd8(0x5ae53) & 0x10)
        sub_29fa2(8);
      else
        sub_29e4e(8);  // original pushes only one arg here
    }
    game_state = st_22d60_addr;
  }
}

// $22d06: abort / restart path used when fire-button == 2 during wait states.
static void st_22d06() {
  in_flight = 1;
  // inline of the short helper at $22ce8
  sub_2d526();
  w_5abc4 = int16_t(uint16_t(w_5abc4) | 0x800);
  sub_36b88();
  b_5ab50 = -1;
  b_5aba6 = 0;
  clear_messages();
  b_5aaa6 = 0;
  b_5aaad = 0;
  b_5ab4f = 1;
  b_5aaac = 1;
  b_5aba5 = 0;
  b_5aa80 = 0;
  state_timer = 5;
  game_state = st_22dc0_addr;
}

// $22d60: enter the in-flight world or return to a short demo wait.
static void st_22d60() {
  if (b_5aa7d == 0) {
    b_5aaac = 1;
    b_5aba6 = 0;
    b_5aba5 = 0;
    b_5aa80 = 0;
    state_timer = 5;
    game_state = st_22dc0_addr;
  } else if (b_5aaa6 < 0) {
    state_timer = 2;
    b_5ab99 = 10;
    game_state = st_23b7e_addr;
  }
}

// $22430 / $2245a / $224ba: combat scenario 2/125 intro messages.
static void st_22430() {
  in_flight = 1;
  b_5ab50 = -1;
  b_5aba5 = 1;
  state_timer = 5;
  game_state = st_2245a_addr;
}

static void st_2245a() {
  if (b_5aa9b != 0) {
    st_224ba();
    return;
  }
  if (state_timer < 0) {
    in_flight = 0;
    uint32_t q = message_queue.ptr();
    if (scenario == 2) {
      wr16(q, 9);
    } else {
      wr16(q, 10);
      b_5aba5 = 0;
    }
    game_state = st_224ba_addr;
  }
}

static void st_224ba() {
  if (b_5aad8 < 0 || b_5aa9b != 0) {
    in_flight = 1;
    b_5ab50 = -1;
    clear_messages();
    b_5aaac = 1;
    b_5aaad = 1;
    b_5aba5 = 0;
    if (scenario == 125) {
      b_5aba6 = 6;
      game_state = st_22d60_addr;
    } else {
      b_5ab2e = 3;
      wr16(0x65092, 0);
      b_5aa80 = 0;
      state_timer = 5;
      game_state = st_22dc0_addr;
    }
  }
}


// $22dc0: build the "ready / airfield status" message queue, then enter $22e56.
static void st_22dc0() {
  if (state_timer >= 0) return;
  in_flight = 0;
  uint32_t q = message_queue.ptr();
  int8_t sel = scenario;
  if (sel >= 3 && sel <= 8) {
    if (int32_t(sub_395dc()) >= 0) {
      wr16(q, 88);
      q += 2;
    }
  }
  if (sel != 2) {
    // $5b480 bit 3: day/night or weather flag
    if (rd8(0x5b480) & 8) {
      wr16(q, 90);
      q += 2;
    } else {
      wr16(q, 89);
      q += 2;
    }
  } else {
    b_5aad8 = -1;
  }
  wr16(q, 0);
  game_state = st_22e56_addr;
}

// $22e56: enable flight systems flags and hand off to the short timer state.
static void st_22e56() {
  if (b_5aad8 >= 0) return;
  in_flight = 1;
  w_5abc4 = int16_t(uint16_t(w_5abc4) | 0x40);
  w_5abc6 = int16_t(uint16_t(w_5abc6) | 0x100);
  w_5abca = int16_t(uint16_t(w_5abca) | 0x2);
  if (scenario == 2) b_5aa8b = 1;
  refresh_system_time();
  wr32(0x5ac0c, rd32(0x5adea));
  wr32(0x5ac04, rd32(0x5adea));
  frame_counter = 0;
  state_timer = 3;
  game_state = st_22ee2_addr;
}

// $22ee2: brief settle, then call the HUD/system init and enter $22f06
// (still original — deep flight-start setup).
static void st_22ee2() {
  if (state_timer < 0) {
    b_5aa99 = 1;
    sub_47d84();
    game_state = st_22f06_addr;
  }
}

// $2353c / $23576: picture-checksum error screen (bad splash/cockpit/front).
static void st_2353c() {
  if (state_timer < 0) {
    load_fade_palette(0x39fd4);
    message_queue = 97;
    state_timer = 500;
    game_state = st_23576_addr;
  } else {
    sub_4492a();
  }
}
static void st_23576() {
  if (b_5aad8 < 0 && state_timer < 0) {
    state_timer = 3;
    game_state = st_23576_addr;  // hold on the error message
  }
}

// $2359e / $235d0: title screen after successful boot check.
static void st_2359e() {
  if (state_timer < 0) {
    load_fade_palette(0x39fd4);
    message_queue = 15;
    game_state = st_235d0_addr;
  } else {
    sub_4492a();
  }
}
static void st_235d0() {
  if (b_5aa9b != 0) {
    music_volume2 = 0x1f0000;
    b_5aacf = 1;  // title_flag
    load_fade_palette(0x3a054);
    clear_messages();
    sub_4492a();
    b_5aacb = 1;
    state_timer = 5;
    scenario = 0;
    sub_2843c();
    game_state = st_2362a_addr;
  }
}

// $2362a: disk / config status messages, then return to mission menu path.
static void st_2362a() {
  sub_4492a();
  state_timer = 5;
  if (b_5aacb == 0) {
    sub_2843c();
    b_5aacd = -1;
    game_state = st_23712_addr;
    return;
  }
  if (disk_state == 0) {
    b_5aacb = 0;
    game_state = st_23712_addr;
    return;
  }
  load_fade_palette(0x39fd4);
  uint32_t q = message_queue.ptr();
  int16_t ds = disk_state;
  if (ds == 1) {
    wr16(q, 68); q += 2;
  } else if (ds == 2) {
    wr16(q, 69); q += 2;
  } else if (ds == 3) {
    wr16(q, 70); q += 2;
  }
  wr16(q, 94); q += 2;
  wr16(q, 0);
  game_state = st_236f6_addr;
}

// $23b7e / $23ba8: short demo-end / return-to-menu after free-flight timer.
static void st_23b7e() {
  in_flight = 0;
  if (b_5ab98 == b_5ab99) {
    message_queue = 38;
    game_state = st_23ba8_addr;
  }
}
static void st_23ba8() {
  if (b_5aaa6 < 0) return;
  in_flight = 1;
  clear_messages();
  b_5ab99 = 15;
  if (b_5aba6 == 6) {
    game_state = st_22d60_addr;
  } else {
    // Pause ended: exclude the paused time from the flight time.
    refresh_system_time();
    int32_t paused = l_5adea - l_5abfc;
    l_5abfc = paused;
    l_5ac00 = l_5ac00 + paused;
    if (l_5ac04 != 0) l_5ac04 = l_5ac04 + l_5abfc;
    l_5abfc = 0;
    game_state = 0x22f06;
  }
}


// $231a4: show message 14, then $23a54 (out-of-fuel / similar).
static void st_231a4() {
  if (state_timer < 0) {
    in_flight = 0;
    message_queue = 14;
    b_5aad8 = 0;
    game_state = st_23a54_addr;
  }
}

// $231d0: brief pause then enter the large end-of-mission handler $231fc
// (still original — complex debrief).
static void st_231d0() {
  if (state_timer < 0) {
    b_5aaa6 = 1;
    state_timer = 2;
    game_state = st_231fc_addr;
    b_5aba5 = 1;
  }
}

// $236f6: disk-error wait — when cleared, retry $2362a.
static void st_236f6() {
  if (b_5aacb == 0) {
    sub_4492a();
    game_state = st_2362a_addr;
  }
}

// $23712: post-title name-entry / first-run setup.
static void st_23712() {
  if (disk_state != 0) {
    clear_messages();
    game_state = st_mission_menu_addr;
    return;
  }
  if (state_timer >= 0) {
    sub_4492a();
    return;
  }
  b_5aba5 = 1;
  load_fade_palette(0x39fd4);
  b_5aad8 = 0;
  b_5aabb = 0;
  uint16_t rank = rd16(config_buf + 4);
  if (rank == 0) {
    message_queue = 1;  // "enter name"
    game_state = st_237f2_addr;
  } else {
    message_queue = 2;  // "welcome back, <name>"
    // copy pilot name from config into the string buffer at $544df
    uint32_t dst = 0x544df + 10;
    uint32_t src = config_buf + 0x1e;
    for (int i = 0; i < 24; ++i) {
      uint8_t c = rd8(src + uint32_t(i));
      if (c == 0) break;
      wr8(dst + uint32_t(i), c);
    }
    game_state = st_23808_addr;
  }
}

static void st_237f2() {
  if (b_5aad8 < 0) game_state = st_23826_addr;
}

static void st_23808() {
  if (b_5aad8 < 0 || b_5aa9b != 0) game_state = st_23826_addr;
}

// $23826: after name entry / welcome, increment rank and go to menu or prompt.
static void st_23826() {
  clear_messages();
  state_timer = 5;
  uint16_t rank = rd16(config_buf + 4);
  if (rank == 0) {
    message_queue = 3;
    b_5aabb = 0;
    b_5aad8 = 2;
    b_5aaed = 20;
    game_state = st_23890_addr;
  } else {
    game_state = st_mission_menu_addr;
  }
  wr16(config_buf + 4, uint16_t(rank + 1));
}

static void st_23890() {
  if (b_5aad8 < 0) {
    clear_messages();
    state_timer = 5;
    game_state = st_mission_menu_addr;
  }
}

// $238e0: in-flight death / eject / mission-fail transition.
static void st_238e0() {
  // Cleanup runs when (aa7d==0 && ab91==0) OR (aa7d!=0 && bit10 of $5b47c clear).
  if (b_5aa7d == 0) {
    if (b_5ab91 != 0) return;
  } else {
    if (rd16(0x5b47c) & (1 << 10)) return;
  }
  w_5abc4 = int16_t(uint16_t(w_5abc4) & 0x9fff);
  w_5abc4 = int16_t(uint16_t(w_5abc4) & 0xfffe);
  sub_2d560();
  b_5ab99 = 15;
  b_5ab81 = 0;
  w_5abcc = int16_t(uint16_t(w_5abcc) & 0xfbff);
  b_5ab8f = int8_t(b_5ab8f - 1);
  if (b_5ab8f <= 0) {
    sub_4492a();
    state_timer = 5;
    in_flight = 0;
    game_state = st_239f8_addr;
  } else {
    game_state = st_23988_addr;
  }
}

static void st_23988() {
  b_5ab92 = 0;
  wr16(0x5b47e, uint16_t(rd16(0x5b47e) | 2));
  if (b_5aa7d == 0) sub_2e662();
  sub_2d560();
  state_timer = 5;
  game_state = st_239ca_addr;
}

static void st_239ca() {
  if (state_timer < 0) {
    w_5abc4 = int16_t(uint16_t(w_5abc4) | 0x40);
    b_5aba8 = -2;
    game_state = st_22f06_addr;
  }
}

// $239f8 / $23a3e: "game over / returned to base" messages → restart.
static void st_239f8() {
  if (state_timer < 0) {
    load_fade_palette(0x39fd4);
    if (b_5ab41 == 16)
      message_queue = 98;
    else
      message_queue = 99;
    b_5ab23 = 0;
    game_state = st_23a3e_addr;
  }
}
static void st_23a3e() {
  if (b_5aad8 < 0) game_state = st_restart_addr;
}

// $23a54 / $23a8c: clear systems, timed pause, then restart.
static void st_23a54() {
  if (b_5aad8 < 0) {
    w_5abc4 = int16_t(uint16_t(w_5abc4) & 0x9fbf);
    state_timer = 100;
    game_state = st_23a8c_addr;
  }
}
static void st_23a8c() {
  if (state_timer < 0) {
    b_5ab22 = 0;
    b_5aa90 = 0;
    game_state = st_restart_addr;
  }
}

// $23ab0 / $23b2c: alternate end-of-flight path (player quit / escape).
static void st_23ab0() {
  if (b_5aad8 < 0) {
    w_5abc4 = int16_t(uint16_t(w_5abc4) & 0x9fbf);
    state_timer = 100;
    game_state = st_23b2c_addr;
    return;
  }
  if (b_5ab22 == -1) {
    b_5ab22 = 0;
    b_5ab23 = 0;
    game_state = st_restart_addr;
  } else if (b_5ab22 == 1) {
    state_timer = -1;
    game_state = st_23b2c_addr;
  }
}
static void st_23b2c() {
  if (state_timer < 0) {
    b_5ab22 = 0;
    b_5aa90 = 0;
    if (b_5aba4 != 0) b_5ab23 = 1;
    b_5aaa6 = 1;
    state_timer = 3;
    in_flight = 0;
    b_5ab99 = 0;
    game_state = st_21a9e_addr;
  }
}


// $22f06: per-frame in-flight state dispatcher — landing detection,
// mission-end keys, and systems re-init when starting a leg.
static void st_22f06() {
  int8_t saved_aa90 = b_5aa90;

  // Landing / touchdown path: bit 9 of $5b47c and low nibble of $5b4f8 clear.
  if ((rd16(0x5b47c) & (1 << 9)) && (rd8(0x5b4f8) & 0x0f) == 0) {
    if (b_5aa84 != 0) {
      // Successful landing: bump mission-complete count, clear flags, debrief.
      wr16(config_buf + 0x10, uint16_t(rd16(config_buf + 0x10) + 1));
      log_dirty = 1;
      w_5abc2 = int16_t(uint16_t(w_5abc2) & 0xfeff);
      w_5abc4 = int16_t(uint16_t(w_5abc4) & 0xffbf);
      wr16(0x5b47c, uint16_t(rd16(0x5b47c) & 0xfdff));
      if (b_5aa7d == 0) {
        b_5ab92 = 1;
        wr32(0x5b4be, 0);
        b_5ab91 = 40;
        stop_all_sounds();
        sub_2a0e4(63, 120);
      }
      game_state = st_238e0_addr;
      return;
    }
    // Touchdown without the success flag: clear landing bit, play event.
    wr16(0x5b47c, uint16_t(rd16(0x5b47c) & 0xfdff));
    post_game_event(0x4021);
    return;
  }

  // Demo timer expired → demo-end state.
  if (b_5aaa6 < 0) {
    state_timer = 2;
    b_5ab99 = 10;
    refresh_system_time();
    wr32(0x5abfc, rd32(0x5adea));
    game_state = st_23b7e_addr;
    return;
  }

  // Player end-of-leg keys encoded in aa90: -1/-2/-4 (and messages pending).
  if (saved_aa90 <= -1 && saved_aa90 >= -4 && b_5aabb != 0 && b_5aad8 != 0) {
    if (saved_aa90 == -1) b_5aa90 = 1;
    else if (saved_aa90 == -2) b_5aa90 = 2;
    else if (saved_aa90 == -4) b_5aa90 = 4;

    in_flight = 1;
    clear_messages();
    b_5aba5 = 0;
    b_5aba8 = -2;
    b_5ab50 = -1;
    b_5aaa6 = 0;

    if (w_5abd4 != 0) {
      w_5abd4 = 0;
      w_5abd6 = 0;
      b_5aa7d = b_5ab2c;
      if (b_5aa7d != 0) {
        w_5ac7e = 50;
        w_5ac7c = 167;
      } else {
        w_5ac7e = 0;
        w_5ac7c = 144;
      }
      b_5aa9f = 0;
      w_5ac80 = int16_t(int16_t(w_5ac7e) << 4);
      arm_flight_systems_flags();
      b_5abaa = int8_t(uint8_t(b_5aab6) & 0x7f);
    }
    return;
  }

  // Explicit quit / abort keys.
  if (b_5ab22 == -1) {
    b_5ab22 = 0;
    b_5aad8 = -1;
    game_state = st_23a54_addr;
    return;
  }
  if (saved_aa90 == -16) {
    game_state = st_23a54_addr;
    return;
  }
  if (saved_aa90 == -17) {
    game_state = st_23ab0_addr;
    return;
  }

  // Default: refresh the key-release wait counter used by the state machine.
  if (w_5abd6 == 0 && b_5aa7d == 0 && b_5ab81 > 5)
    b_5ab99 = 2;
  else
    b_5ab99 = 15;
}


// $231fc: end-of-mission debrief — build a message queue from how the
// flight ended ($5aa90) and the selected scenario, then return control
// to the in-flight dispatcher ($22f06) which reacts to the aa90 codes.
static void st_231fc() {
  if (state_timer >= 0) return;

  int16_t sel = int16_t(int8_t(scenario));
  in_flight = 0;
  game_state = st_22f06_addr;
  clear_messages();
  uint32_t q = message_queue.ptr();
  int8_t end = b_5aa90;

  if (end == -1) {
    // Successful completion / landing.
    if (sel >= 3 && sel <= 8) {
      sub_39510(uint32_t(sel), 0, 4);
      q = message_queue.ptr() + 2;
      wr16(q, sel == 3 ? uint16_t(0x8055) : uint16_t(0x8056));
      q += 2;
    } else if (sel == 9) {
      wr16(config_buf, 1);
      sub_28592();
      q = message_queue.ptr();
      wr16(q, 74); q += 2;
      wr16(q, 0x8053); q += 2;
      b_5ab23 = 1;
      b_5aa90 = -17;
    } else if (sel == 125) {
      wr16(q, 72); q += 2;
      b_5aa90 = -16;
    } else {
      wr16(q, 65); q += 2;
    }
  } else if (end == -2) {
    // Failed / aborted — scenario-specific debrief text.
    int32_t idx = int32_t(sel) - 4;
    if (idx >= 0 && idx < 4) {
      switch (idx) {
        case 0:
          wr16(q, (frame_counter & 1) ? 36 : 37);
          q += 2;
          break;
        case 1:
          wr16(q, 44); q += 2;
          b_5aa90 = -16;
          break;
        case 2:
          wr16(q, 51); q += 2;
          break;
        case 3:
          wr16(q, 57); q += 2;
          if (frame_counter & 1) {
            wr16(q, 0x803a); q += 2;
            b_5aa90 = -16;
          } else {
            wr16(q, 0x805c); q += 2;
          }
          break;
      }
    } else {
      wr16(q, 66); q += 2;
    }
    if (b_5aa90 != -16) {
      // (the original skips one slot first: the word at q keeps its value)
      q += 2;
      wr16(q, 0x8056); q += 2;
      wr16(q, 0x8053); q += 2;
    }
  } else if (end == -3 && sel == 4) {
    wr16(q, 76); q += 2;
    b_5aa90 = -16;
  }

  if (b_5aa90 == -4) {
    wr16(q, 84); q += 2;
    wr16(q, 0x8053); q += 2;
    count_mission_complete();
    sub_28592();
  }
  wr16(q, 0);
}

}  // namespace native
