// Assembly part of the per-frame flight/scene update (called from game_frame
// $2112c), register-convention routines translated to work on the live
// register file so that leftover registers match the original:
//
//   $39a54  heading -> octant 0..7 in $5ab4c
//   $2f4a6  camera position/eye offset from the player record -> $5ad5a/$5ad74/$5ad6a
//   $2f74e  nearness class of the player record ($5abb4 = 1/3/5)
//   $2f596  detect view changes -> flag byte $5ab50
//   $2f9da  mark all object/scenario slots dirty (bit 4 of the flag byte)
//   $2f7b8  scene update: rebuild lists ($30064), draw ($3127c, $31494, ...)
//   $3127c  object-list expansion and depth sort of the visible objects
//   $3a310/$3a312  empty stubs
#include "native/asmregs.hpp"
#include "native/orig.hpp"
#include "native/vars.hpp"

namespace native {
using namespace asmr;

namespace {

constexpr uint32_t scenario_base = 0x5b47c;   // 16 records of 512 bytes
constexpr uint32_t record_base = 0x5d47c;     // 16 records of 32 bytes

int16_t sw(uint32_t a) { return int16_t(rd16(a)); }
int32_t sl(uint32_t a) { return int32_t(rd32(a)); }
uint32_t mulsw_mem(uint16_t r, uint32_t a) { return uint32_t(int32_t(int16_t(r)) * sw(a)); }

// Original routines that never return (error trap: store a code, loop).
[[noreturn]] void error_loop(uint16_t code) {
  wr16(0x5ac96, code);
  for (;;) call_c(0x2f216);
}

// ---- $39a54 ---------------------------------------------------------------
void heading_octant() {
  if (rd8(0x5aa7d) != 0) setw(0, rd16(0x5ad8e));
  else D(0) = rd32(0x5ad84);
  int16_t h = int16_t(lo(0));
  uint32_t o;
  if (h < 14400) {
    if (h < 7200) o = h < 3600 ? 0 : 1;
    else o = h < 10800 ? 2 : 3;
  } else {
    if (h < 21600) o = h < 18000 ? 4 : 5;
    else o = h < 25200 ? 6 : 7;
  }
  D(1) = o;
  wr8(0x5ab4c, o);
  flags(o, 8);
}
NATIVE_ASM(0x39a54, heading_octant, true);

void stub_3a310() {}
void stub_3a312() {}
NATIVE_ASM(0x3a310, stub_3a310, true);
NATIVE_ASM(0x3a312, stub_3a312, true);

// ---- $2f4a6 ---------------------------------------------------------------
void camera_position() {
  if (rd8(0x5aa7d) != 0) {
    call_c(0x2f220);
    D(0) = rd32(0x5af36);
    D(1) = rd32(0x5af3a);
    D(2) = rd32(0x5af3e);
    for (int i = 0; i < 3; ++i) wr32(0x5ad74 + 4u * uint32_t(i), D(i));
    D(0) = rd32(0x5ad5a);
    D(1) = rd32(0x5ad5e);
    D(2) = rd32(0x5ad62);
  } else {
    uint32_t a2 = scenario_base + uint32_t(int32_t(int16_t(rd16(0x5abd6))));
    A(2) = a2;
    if (rd8(a2 + 98) == 48) {
      D(3) = 0; D(4) = 1; D(5) = uint32_t(-5);
    } else {
      D(3) = 0;
      if (rd8(a2 + 98) == 17) { D(4) = 5; D(5) = 20; }
      else { D(4) = 4; D(5) = 18; }
    }
    setw(6, lo(3)); setw(0, lo(4)); setw(7, lo(5));
    D(6) = mulsw_mem(lo(6), a2 + 146);
    D(0) = mulsw_mem(lo(0), a2 + 148);
    D(7) = mulsw_mem(lo(7), a2 + 150);
    D(0) += D(6);
    D(0) += D(7);
    setw(6, lo(3)); setw(1, lo(4)); setw(7, lo(5));
    D(6) = mulsw_mem(lo(6), a2 + 152);
    D(1) = mulsw_mem(lo(1), a2 + 154);
    D(7) = mulsw_mem(lo(7), a2 + 156);
    D(1) += D(6);
    D(1) += D(7);
    setw(2, lo(4));
    D(3) = mulsw_mem(lo(3), a2 + 158);
    D(2) = mulsw_mem(lo(2), a2 + 160);
    D(5) = mulsw_mem(lo(5), a2 + 162);
    D(2) += D(3);
    D(2) += D(5);
    for (int i = 0; i < 3; ++i) D(i) = uint32_t(asr32(int32_t(D(i)), 6));
    D(3) = rd32(a2 + 20);
    D(4) = rd32(a2 + 24);
    D(5) = rd32(a2 + 28);
    D(3) += D(0);
    D(4) += D(1);
    D(5) += D(2);
    wr32(0x5ad74, D(3));
    wr32(0x5ad78, D(4));
    wr32(0x5ad7c, D(5));
    D(3) = rd32(a2 + 20) & 0x3FFFFF;
    D(0) += D(3);
    D(1) += rd32(a2 + 24);
    D(3) = rd32(a2 + 28) & 0x3FFFFF;
    D(2) += D(3);
    D(0) = uint32_t(-int32_t(D(0)));
    D(1) = uint32_t(-int32_t(D(1)));
    D(2) = uint32_t(-int32_t(D(2)));
    wr32(0x5ad5a, D(0));
    wr32(0x5ad5e, D(1));
    wr32(0x5ad62, D(2));
  }
  for (int i = 0; i < 3; ++i) D(i) = uint32_t(asr32(int32_t(D(i)), 8));
  wr16(0x5ad6a, lo(0));
  wr16(0x5ad6c, lo(1));
  wr16(0x5ad6e, lo(2));
  wr32(0x5ad70, D(1));
}
NATIVE_ASM(0x2f4a6, camera_position, true);

// ---- $2f74e (a3 = player record) ------------------------------------------
void nearness_class() {
  uint32_t a3 = A(3);
  setw(0, rd16(a3 + 86));
  if (int16_t(lo(0)) < 0) setw(0, uint16_t(-int16_t(lo(0))));
  setw(1, rd16(a3 + 88));
  if (int16_t(lo(1)) < 0) setw(1, uint16_t(-int16_t(lo(1))));
  setw(2, rd16(a3 + 90));
  if (int16_t(lo(2)) < 0) setw(2, uint16_t(-int16_t(lo(2))));
  setw(2, asr16(lo(2), 2));
  if (int16_t(lo(1)) > int16_t(lo(0))) {
    if (int16_t(lo(1)) > int16_t(lo(2))) setw(0, lo(1));
    else setw(0, lo(2));
  } else if (int16_t(lo(2)) > int16_t(lo(0))) {
    setw(0, lo(2));
  }
  if (int16_t(lo(0)) > 96) {
    wr8(0x5abb4, int16_t(lo(0)) > 192 ? 1 : 3);
    return;
  }
  setw(1, rd16(a3 + 108));
  if (int16_t(lo(1)) < 0) setw(1, uint16_t(-int16_t(lo(1))));
  wr8(0x5abb4, int16_t(lo(1)) > 4096 ? 3 : 5);
}
NATIVE_ASM(0x2f74e, nearness_class, true);

// ---- $2f596 ---------------------------------------------------------------
void view_change_flags() {
  D(5) = 0;
  setb(0, rd8(0x5ab4c));
  if (uint8_t(D(0)) != rd8(0x5ab4d)) {
    wr8(0x5ab4d, D(0));
    if (rd8(0x5aa7e) == 0) setb(5, uint8_t(D(5)) | 11);
  }
  D(0) = uint32_t(-int32_t(rd32(0x5ad5e)));
  bool far_now = int32_t(D(0)) >= 40960;
  bool far_before = sl(0x5ad66) >= 40960;
  if (far_now != far_before) setb(5, uint8_t(D(5)) | 11);
  wr32(0x5ad66, D(0));
  D(0) = (D(0) & 0xFFFF0000u) >> 16;          // clrw d0; swap d0
  D(0) = uint32_t(asr32(int32_t(D(0)), 5));
  if (lo(0) != rd16(0x5ad56)) {
    if (rd8(0x5aba6) != 2) setb(5, uint8_t(D(5)) | 11);
    wr16(0x5ad56, lo(0));
  }
  call_c(0x36b88);
  if (rd8(0x5aa7d) == 0) {
    A(3) = scenario_base + uint32_t(int32_t(int16_t(rd16(0x5abd6))));
    nearness_class();
    wr16(0x5ac44, rd16(A(3) + 6));
    wr16(0x5ac46, rd16(A(3) + 8));
    wr8(0x5ab49, rd8(A(3) + 10));
    setw(0, rd16(0x5ac44) & 3);
    setw(0, lo(0) - 3);
    setw(0, uint16_t(-int16_t(lo(0))));
    setw(1, rd16(0x5ac46) & 3);
    setw(1, lo(1) - 3);
    setw(1, uint16_t(-int16_t(lo(1))));
    setw(1, uint16_t(lo(1) << 2));
    setw(1, lo(1) + lo(0));
    wr8(0x5ab48, lo(1));
  } else {
    cpu().push16(uint16_t(D(5)));
    call_c(0x3d82e);
    setw(5, cpu().pop16());
    A(3) = scenario_base;
    nearness_class();
    D(0) = rd32(0x5af36);
    D(1) = rd32(0x5af3e);
    D(0) &= 0x1FFFFFFF;
    D(1) &= 0x1FFFFFFF;
    D(0) = (D(0) << 16) | (D(0) >> 16);
    D(1) = (D(1) << 16) | (D(1) >> 16);
    setw(0, asr16(lo(0), 4));
    setw(1, asr16(lo(1), 4));
    setw(2, lo(0));
    setw(3, lo(1));
    setw(0, lo(0) & 3);
    setw(1, lo(1) & 3);
    setb(0, uint8_t(uint8_t(D(0)) - 3));
    setb(0, uint8_t(-int8_t(uint8_t(D(0)))));
    setb(1, uint8_t(uint8_t(D(1)) - 3));
    setb(1, uint8_t(-int8_t(uint8_t(D(1)))));
    setb(1, uint8_t(D(1)) + uint8_t(D(1)));
    setb(1, uint8_t(D(1)) + uint8_t(D(1)));
    setb(1, uint8_t(D(1)) + uint8_t(D(0)));
    setw(2, asr16(lo(2), 2));
    setw(3, asr16(lo(3), 2));
    setw(0, lo(2));
    setw(4, lo(3));
    setw(2, lo(2) & 3);
    setw(3, lo(3) & 3);
    setb(2, uint8_t(uint8_t(D(2)) - 3));
    setb(2, uint8_t(-int8_t(uint8_t(D(2)))));
    setb(3, uint8_t(uint8_t(D(3)) - 3));
    setb(3, uint8_t(-int8_t(uint8_t(D(3)))));
    setb(3, uint8_t(D(3)) + uint8_t(D(3)));
    setb(3, uint8_t(D(3)) + uint8_t(D(3)));
    setb(3, uint8_t(D(3)) + uint8_t(D(2)));
    auto unsettled = [&] {
      return rd8(0x5aba6) != 2 || sl(0x5ad70) > -4096;
    };
    if (uint8_t(D(1)) != rd8(0x5ab49)) {
      if (unsettled()) D(5) = 0xFFFFFFFFu;
      wr8(0x5ab49, D(1));
    }
    if (uint8_t(D(3)) != rd8(0x5ab48)) {
      if (unsettled()) D(5) = 0xFFFFFFFFu;
      wr8(0x5ab48, D(3));
    }
    if (lo(0) != rd16(0x5ac44)) {
      D(5) = 0xFFFFFFFFu;
      wr16(0x5ac44, lo(0));
    }
    if (lo(4) != rd16(0x5ac46)) {
      D(5) = 0xFFFFFFFFu;
      wr16(0x5ac46, lo(4));
    }
  }
  wr8(0x5ab50, rd8(0x5ab50) | uint8_t(D(5)));
}
// Not verified per call: $36b88 draws a random number from the beam position,
// which differs between the reference and native runs (fewer cycles elapse in
// the native routine); the equivalence run (deterministic timing) covers it.
NATIVE_ASM(0x2f596, view_change_flags, false);

// ---- $2f9da ---------------------------------------------------------------
void mark_all_dirty() {
  A(0) = scenario_base;
  setb(7, 16);
  for (uint32_t i = 0; i < 16; ++i) wr8(scenario_base + 1 + 512 * i, rd8(scenario_base + 1 + 512 * i) | 16);
  A(0) = record_base;
  for (uint32_t i = 0; i < 16; ++i) wr8(record_base + 1 + 32 * i, rd8(record_base + 1 + 32 * i) | 16);
}
NATIVE_ASM(0x2f9da, mark_all_dirty, true);

// ---- $313fa (a2/d7 in): remove duplicate depth keys ---------------------------
// Copies the word keys at $63a68 (d7 entries) to $63a94 and then, for every
// run of keys, writes -1 over the key and moves the word 44 bytes above to the
// result list at $63a94+132.  Registers are saved/restored by the original.
void depth_dedupe(uint16_t count) {
  uint32_t a5 = 0x63a94;
  for (uint32_t i = 0; i < 11 * 4; i += 4) wr32(a5 + i, rd32(0x63a68 + i));  // d0-d6/a1-a4 = 11 longs
  uint32_t a4 = a5 + 132;
  int16_t d1 = int16_t(count - 1);
  for (;;) {                                           // $31418
    uint32_t a0 = a5;
    int16_t d6 = d1;
    int16_t d0;
    for (;;) {                                         // find first key >= 0
      d0 = int16_t(rd16(a0));
      a0 += 2;
      if (d0 >= 0) break;
      if (--d6 == -1) return;
    }
    uint32_t a1 = a0 - 2;                              // $31426
    if (--d6 < 0) {
      wr16(a4, rd16(a1 + 44));
      return;
    }
    for (;;) {                                         // $31434
      int16_t k = int16_t(rd16(a0));
      a0 += 2;
      if (d0 < k) {                                    // cmpw a0@+,d0; blts
        a1 = a0 - 2;
        d0 = int16_t(rd16(a1));
      }
      if (--d6 == -1) break;
    }
    wr16(a1, 0xFFFF);                                  // $31448
    wr16(a4, rd16(a1 + 44));
    a4 += 2;
  }
}

// ---- $3127c ---------------------------------------------------------------
void scene_expand_3127c() {
  m68k::Cpu& c = cpu();
  // moveml d0-a5,-(sp): d0-d7 then a0-a5 at ascending addresses.
  c.a[7] -= 56;
  for (int i = 0; i < 8; ++i) wr32(c.a[7] + 4u * uint32_t(i), c.d[i]);
  for (int i = 0; i < 6; ++i) wr32(c.a[7] + 32 + 4u * uint32_t(i), c.a[i]);
  auto restore = [&] {
    for (int i = 0; i < 8; ++i) c.d[i] = rd32(c.a[7] + 4u * uint32_t(i));
    for (int i = 0; i < 6; ++i) c.a[i] = rd32(c.a[7] + 32 + 4u * uint32_t(i));
    c.a[7] += 56;
  };
  if (rd8(0x5aa9d) == 0) { restore(); return; }
  uint32_t fp = c.a[6];
  for (;;) {
    A(0) = 0x5acc6;
    int8_t before = int8_t(rd8(0x5ab55));
    setb(0, uint8_t(before - 1));
    if (before - 1 >= 0) {
      setw(0, uint16_t(int16_t(int8_t(uint8_t(D(0))))));
      setw(1, lo(0));
      setw(0, lo(0) + lo(0));
      setw(0, lo(0) + lo(0));
      setw(1, lo(1) + lo(1));
      setw(0, lo(0) + lo(1));
      A(0) += uint32_t(ext(lo(0)));
      if (int16_t(rd16(A(0))) < 0) { restore(); return; }
      A(3) = 0x63a68;
      A(1) = rd32(A(0));
      A(0) += 4;
      A(2) = A(1);
      setw(0, rd16(A(0)));
      A(0) += 2;
      if (int16_t(lo(0)) <= 0) error_loop(55);
      setw(7, lo(0));
      if (int16_t(lo(7)) > 22) setw(7, 22);
      setw(0, lo(7));
      setw(0, lo(0) - 1);
      do {                                             // $312ce
        setw(1, rd16(A(1)));
        A(1) += 2;
        setw(2, lo(1));
        setw(2, lo(2) & 15);
        wr16(0x5adb0, lo(2));
        if (lo(1) & 0x40) {
          setw(1, 32767);
          A(1) += 10;
        } else {
          setw(3, rd16(A(1) + 14));
          if (lo(3) != 0) {
            setw(3, asl16(lo(3), D(2)));
            A(1) += 10;
            setw(1, lo(3));
          } else {                                     // $31310
            A(1) += 4;
            D(2) = uint32_t(ext(rd16(A(1))));          // movemw sign-extends
            D(3) = uint32_t(ext(rd16(A(1) + 2)));
            D(4) = uint32_t(ext(rd16(A(1) + 4)));
            A(1) += 6;
            wr8(0x5abb3, 0);
            if (lo(1) & 0x10) {
              A(0) = scenario_base;
              setw(6, lo(1));
              setw(1, lo(1) & 0xFF00);
              setw(1, lo(1) + lo(1));
              A(0) += uint32_t(ext(lo(1)));
              setw(6, lo(6) & 15);
              setw(3, rd16(A(0) + 12) & 4095);
              setw(3, asr16(lo(3), D(6)));
              setw(2, lo(2) + lo(3));
              setw(3, rd16(A(0) + 14) & 4095);
              setw(3, asr16(lo(3), D(6)));
              setw(4, lo(4) + lo(3));
              D(3) = rd32(A(0) + 16);
              D(1) = D(3);
              D(3) = uint32_t(asr32(int32_t(D(3)), D(6)));
              D(1) += rd32(0x5ad70);
              D(1) = uint32_t(asr32(int32_t(D(1)), D(6)));
              wr32(0x5ae34, D(1));
              wr8(0x5abb3, 1);
            }
            call_c(0x30872);
            setw(2, rd16(0x5adb0));
            setw(1, asl16(lo(1), D(2)));
          }
        }
        wr16(A(3), lo(1));                             // $3138a
        A(3) += 2;
        A(1) += 12;
        setw(0, lo(0) - 1);
      } while (lo(0) != 0xFFFF);
      // $31394: depth-sort bookkeeping
      depth_dedupe(lo(7));
      A(1) = A(2);
      A(5) = 0x5d684;
      A(4) = A(5);
      setw(7, lo(7) - 1);
      setw(6, lo(7));
      do {
        for (int i = 0; i < 6; ++i) D(i) = rd32(A(2) + 4u * uint32_t(i));
        A(2) += 24;
        for (int i = 0; i < 6; ++i) wr32(A(5) + 4u * uint32_t(i), D(i));
        A(5) += 24;
        setw(6, lo(6) - 1);
      } while (lo(6) != 0xFFFF);
      A(3) = 0x63b18;
      do {
        setw(4, rd16(A(3)));
        A(3) += 2;
        setw(4, uint16_t(lo(4) << 3));
        setw(5, lo(4));
        setw(4, lo(4) + lo(4));
        setw(4, lo(4) + lo(5));
        uint32_t src = A(4) + uint32_t(ext(lo(4)));
        for (int i = 0; i < 6; ++i) D(i) = rd32(src + 4u * uint32_t(i));
        for (int i = 0; i < 6; ++i) wr32(A(1) + 4u * uint32_t(i), D(i));
        A(1) += 24;
        setw(7, lo(7) - 1);
      } while (lo(7) != 0xFFFF);
    }
    // $313d8
    int8_t nb = int8_t(rd8(0x5ab55));
    wr8(0x5ab55, uint8_t(nb - 1));
    if (int(nb) - 1 < 0) {
      wr8(0x5ab55, rd8(0x5ab54));
      break;
    }
    if (rd8(fp - 44) == 0) break;
  }
  restore();
}
NATIVE_ASM(0x3127c, scene_expand_3127c, true);

// ---- $2f7b8 ---------------------------------------------------------------
// Scene update.  Writes a "scene list rebuilt" flag to -44(a6) of the
// *calling* frame (game_frame), which $3127c reads back.
void scene_update() {
  m68k::Cpu& c = cpu();
  if (sl(0x5ad5e) < -134217728) return;
  uint32_t fp = c.a[6];
  c.a[7] -= 28;                                         // moveml d0-d5/a0,-(sp)
  for (int i = 0; i < 6; ++i) wr32(c.a[7] + 4u * uint32_t(i), c.d[i]);
  wr32(c.a[7] + 24, c.a[0]);
  wr8(fp - 44, 0);
  setb(0, rd8(0x5ab50));
  if (uint8_t(D(0)) != 0) {
    wr16(0x5adcc, 73);
    call_c(0x2f9da);
    setb(1, uint8_t(D(0)));
    setb(1, uint8_t(D(1)) & 15);
    uint8_t k = uint8_t(D(1));
    if (k != 12 && k != 11 && k != 15) {
      wr16(0x5ac96, 39);
      call_c(0x2f216);
    }
    if (rd8(0x5aa7d) != 0) {
      D(1) = rd32(0x5af36) & 0x1FFFFFFF;
      D(2) = rd32(0x5af3e) & 0x1FFFFFFF;
      D(1) = (D(1) << 16) | (D(1) >> 16);
      D(2) = (D(2) << 16) | (D(2) >> 16);
      setw(1, asr16(lo(1), 8));
      setw(2, asr16(lo(2), 8));
    } else {
      A(1) = scenario_base + uint32_t(int32_t(int16_t(rd16(0x5abd6))));
      setw(1, rd16(A(1) + 6));
      setw(2, rd16(A(1) + 8));
      setw(1, asr16(lo(1), 2));
      setw(2, asr16(lo(2), 2));
    }
    wr16(0x5ac40, lo(1));
    wr16(0x5ac42, lo(2));
    wr8(0x5ab51, 1);
    wr8(0x5ab5c, 0);
    wr8(0x5ab5d, 0);
    if (D(0) & 1) {
      wr8(0x5ab50, rd8(0x5ab50) & ~1);
      call_c(0x30064);
    }
    if (rd8(0x5ab50) & 2) {
      wr8(0x5ab50, rd8(0x5ab50) & ~2);
      wr8(0x5ab5d, 1);
      call_c(0x30064);
    }
    wr8(0x5ab5c, 1);
    if (rd8(0x5ab50) & 4) wr8(0x5ab50, rd8(0x5ab50) & ~4);
    if (rd8(0x5ab50) & 8) {
      wr8(0x5ab50, rd8(0x5ab50) & ~8);
      call_c(0x30064);
      wr16(0x5adcc, 74);
    }
    wr8(fp - 44, 1);
    wr8(0x5ab50, 0);
  }
  call_c(0x3127c);
  wr16(0x5adcc, 75);
  wr8(fp - 44, 0);
  call_c(0x31494);
  wr16(0x5adcc, 76);
  setw(1, rd16(0x5abd2) & 1);
  if (lo(1) == 0) {
    call_c(0x33120);
    wr16(0x5adcc, 77);
  } else {
    call_c(0x33140);
    wr16(0x5adcc, 78);
  }
  for (int i = 0; i < 6; ++i) c.d[i] = rd32(c.a[7] + 4u * uint32_t(i));   // moveml (sp)+
  c.a[0] = rd32(c.a[7] + 24);
  c.a[7] += 28;
  if (rd8(0x5aa9f) == 0 && rd8(0x5aa9c) != 0) {
    setw(0, 114);
    setw(1, 160);
    if (rd8(0x5ab51) != 0) {
      wr32(0x5a9de, 1048575);
      wr16(0x5ac4c, 15);
    } else {
      wr16(0x5ac4c, 6);
    }
    call_c(0x44276);
  }
  wr8(0x5ab51, 0);
}
NATIVE_ASM(0x2f7b8, scene_update, false);  // many callees; checked by the equivalence run

}  // namespace
}  // namespace native
