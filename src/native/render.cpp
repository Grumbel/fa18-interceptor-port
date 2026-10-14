// Object transform / polygon-list driver of the 3D renderer ($31c50-$33160).
//
// Register-convention assembly translated statement by statement onto the
// live register file (all leftover registers and condition codes that
// callers can observe are reproduced).  Entry points registered here:
//   $31c8c / $31c9c / $31d64   object draw entries (called through the
//                              object type tables); share the body at $31d6c
//   $32d64 / $32d72            distant-object variant ($32d7e body)
//   $32ffa / $33052 / $3309e   polygon-list ops (jump table at $33478)
//   $33120 / $33140            sky/ground colour detection ($33160 helper)
//   $31c50                     view-offset helper
// The bulk of the code are `link a6,#-152` frames: the locals live in real
// stack memory because the ops dispatched through $33478 and the helper
// routines address the caller's frame through a6.
//
// Frame locals (offsets from a6):
//   -4 w  -6 w (fraction bits)  -8 w (shift)  -10 w (vertex count)
//   -20/-18/-16 w view origin   -32/-28/-24 l scaled eye position
//   -38/-36/-34 w negated eye   -40 w distance limit  -44 l object base
//   -48 w  -86..-84 w  -96 w  -98 w  -100 b  -102/-104/-106/-108 w
//   -114 l  -120/-118/-116 w  -122 w  -124 w (result bits)
//   -127 b (bit 0: "draw as picture")  -128 w  -134/-132/-130 w  -136 b  -150 w
#include "native/asmregs.hpp"
#include "native/orig.hpp"
#include "native/vars.hpp"

namespace native {
using namespace asmr;

namespace {

int16_t sw(uint32_t a) { return int16_t(rd16(a)); }
int32_t sl(uint32_t a) { return int32_t(rd32(a)); }
int32_t e16(uint16_t v) { return int16_t(v); }
uint32_t FP() { return cpu().a[6]; }
uint16_t RW(int32_t off) { return rd16(FP() + uint32_t(off)); }
void WW(int32_t off, uint32_t v) { wr16(FP() + uint32_t(off), v); }
uint32_t RL(int32_t off) { return rd32(FP() + uint32_t(off)); }
void WL(int32_t off, uint32_t v) { wr32(FP() + uint32_t(off), v); }
uint8_t RB(int32_t off) { return rd8(FP() + uint32_t(off)); }
void WB(int32_t off, uint32_t v) { wr8(FP() + uint32_t(off), v); }

void negw(int r) { setw(r, uint16_t(-int16_t(lo(r)))); }
void negl(int r) { D(r) = uint32_t(-int32_t(D(r))); }
void movemw(uint32_t a, int r0, int n) {
  for (int i = 0; i < n; ++i) D(r0 + i) = uint32_t(e16(rd16(a + 2u * uint32_t(i))));
}
void movemw_store(uint32_t a, std::initializer_list<uint32_t> v) {
  uint32_t i = 0;
  for (uint32_t x : v) { wr16(a + 2 * i, x); ++i; }
}
bool lt_flags() { return cpu().fn != cpu().fv; }  // blt after a call returned
// mulsw (a<m>)+,d<r>
uint32_t mm(int r, int m) {
  uint32_t v = uint32_t(int32_t(int16_t(lo(r))) * sw(A(m)));
  A(m) += 2;
  return v;
}
void row3(int s0, int s1, int s2, int m) {
  setw(5, lo(s0));
  setw(6, lo(s1));
  setw(7, lo(s2));
  D(5) = mm(5, m);
  D(6) = mm(6, m);
  D(7) = mm(7, m);
  D(7) += D(6);
  D(7) += D(5);
  D(7) = uint32_t(asr32(int32_t(D(7)), 8));
}
void last3(int m) {
  D(2) = mm(2, m);
  D(3) = mm(3, m);
  D(4) = mm(4, m);
  D(4) += D(3);
  D(4) += D(2);
  D(4) = uint32_t(asr32(int32_t(D(4)), 8));
}
void rot_out(int m) {                                // three words to (a3)+
  row3(2, 3, 4, m);
  wr16(A(3), lo(7));
  A(3) += 2;
  row3(2, 3, 4, m);
  wr16(A(3), lo(7));
  A(3) += 2;
  last3(m);
  wr16(A(3), lo(4));
  A(3) += 2;
}
// x/z only rotation ("2d" rows): d5 = d2*M0, skip one, d7 = d4*M2
void row_xz(int m, int add_reg_word_from_d) {
  setw(5, lo(2));
  setw(7, lo(4));
  D(5) = mm(5, m);
  A(m) += 2;
  D(7) = mm(7, m);
  D(7) += D(5);
  D(7) = uint32_t(asr32(int32_t(D(7)), 8));
  setw(7, lo(7) + lo(add_reg_word_from_d));
  wr16(A(3), lo(7));
  A(3) += 2;
}
void last_xz(int m, uint16_t add) {
  D(2) = mm(2, m);
  D(4) = uint32_t(int32_t(int16_t(lo(4))) * sw(A(m) + 2));
  D(4) += D(2);
  D(4) = uint32_t(asr32(int32_t(D(4)), 8));
  setw(4, lo(4) + add);
  wr16(A(3), lo(4));
  A(3) += 2;
}
void push16(uint16_t v) { cpu().push16(v); }
uint16_t pop16() { return cpu().pop16(); }

constexpr uint32_t scenario_base = 0x5b47c;

[[noreturn]] void error_loop(uint16_t code) {
  wr16(0x5ac96, code);
  for (;;) call_c(0x2f216);
}

void leave_frame() {
  cpu().a[7] = cpu().a[6];
  cpu().a[6] = cpu().pop32();
}
void ret0_leave() {                                  // unlk a6; moveq #0,d0; rts
  leave_frame();
  D(0) = 0;
  flags(0, 32);
}

// ---- $3223e: project the three words at a1(-6) through the matrix $5af18 ----
void s3223e() {
  movemw(A(1) - 6, 2, 3);
  setw(3, lo(3) - 40);
  setw(4, lo(4) - 163);
  setw(7, RW(-8));
  setw(2, asr16(lo(2), D(7)));
  setw(3, asr16(lo(3), D(7)));
  setw(4, asr16(lo(4), D(7)));
  A(2) = 0x5af18;
  rot_out(2);
}

// ---- $32adc / $32aec / $32b24: plane tests (return Z like the original) ----
bool s32aec() {                                       // d0-d5 in; returns Z flag
  setw(7, rd16(0x5adb0));
  setw(0, asr16(lo(0), D(7)));
  setw(1, asr16(lo(1), D(7)));
  setw(2, asr16(lo(2), D(7)));
  setw(0, lo(0) + rd16(0x5ae22));
  setw(2, lo(2) + rd16(0x5ae26));
  setw(0, lo(0) - RW(-38));
  setw(1, lo(1) - RW(-36));
  setw(2, lo(2) - RW(-34));
  D(0) = uint32_t(int32_t(int16_t(lo(0))) * int16_t(lo(3)));
  D(1) = uint32_t(int32_t(int16_t(lo(1))) * int16_t(lo(4)));
  D(2) = uint32_t(int32_t(int16_t(lo(2))) * int16_t(lo(5)));
  D(2) += D(0);
  D(2) += D(1);
  if (int32_t(D(2)) < 0) {
    setw(7, 0);
    flags(0, 16);
    return true;
  }
  D(7) = 1;
  flags(1, 32);
  return false;
}
bool s32b92();
bool s32adc() {
  setw(7, lo(7) & 12288);
  if (lo(7) != 0) {
    A(0) = RL(-44);
    A(0) += uint32_t(e16(rd16(A(2))));
    A(2) += 2;
    movemw(A(0), 0, 6);
    return s32aec();
  }
  A(0) = 0x61288;                                     // $32b24
  movemw(A(0), 0, 6);
  setw(3, lo(3) - lo(0));
  setw(4, lo(4) - lo(1));
  setw(5, lo(5) - lo(2));
  movemw(A(0) + 12, 6, 2);
  setw(6, lo(6) - lo(0));
  setw(7, lo(7) - lo(1));
  setw(0, rd16(A(0) + 16));
  setw(0, lo(0) - lo(2));
  setw(1, uint16_t(A(3)));
  setw(1, asr16(lo(1), 7));
  setw(1, lo(1) & 7);
  if (lo(1) != 0) {
    setw(3, asl16(lo(3), D(1)));
    setw(4, asl16(lo(4), D(1)));
    setw(5, asl16(lo(5), D(1)));
    setw(6, asl16(lo(6), D(1)));
    setw(7, asl16(lo(7), D(1)));
    setw(0, asl16(lo(0), D(1)));
  }
  auto m = [](int a, int b) {
    return uint32_t(int32_t(int16_t(lo(a))) * int16_t(lo(b)));
  };
  setw(1, lo(5));
  setw(2, lo(0));
  D(0) = m(0, 4);
  D(5) = m(5, 7);
  D(0) -= D(5);
  D(0) = uint32_t(asr32(int32_t(D(0)), 8));
  D(2) = m(2, 3);
  D(1) = m(1, 6);
  D(1) -= D(2);
  D(1) = uint32_t(asr32(int32_t(D(1)), 8));
  D(7) = m(7, 3);
  D(6) = m(6, 4);
  D(7) -= D(6);
  D(7) = uint32_t(asr32(int32_t(D(7)), 8));
  D(0) = mm(0, 0);
  D(1) = mm(1, 0);
  D(7) = uint32_t(int32_t(int16_t(lo(7))) * sw(A(0)));
  D(7) += D(0);
  D(7) += D(1);
  if (int32_t(D(7)) < 0) {
    setw(7, 0);
    flags(0, 16);
    return true;
  }
  D(7) = 1;
  flags(1, 32);
  return false;
}
// $32b92: plane test of the object header (d0 index, d7 flag bits); the
// original returns the Z flag of its last instruction (btst/moveq/clrw).
bool s32b92() {
  A(0) = rd32(0x5ad2a);
  setw(1, lo(7));
  setw(7, lo(7) & 3072);
  setw(7, asr16(lo(7), 8));
  setw(7, asr16(lo(7), 2));
  setw(7, lo(7) - 1);
  bool less;                                          // blts $32c20 condition
  if (lo(7) == 0) {                                   // $32bfe
    setb(5, rd8(A(0) + 6));
    setw(5, lo(5) & 15);
    setw(2, rd16(A(0) + 12 + uint32_t(e16(lo(0)))));
    setw(2, asr16(lo(2), D(5)));
    D(7) = rd32(0x5ad70);
    negl(7);
    extl(2);
    less = int32_t(D(7)) < int32_t(D(2));
  } else {
    setw(7, lo(7) - 1);
    uint32_t off;
    uint32_t src;
    if (lo(7) == 0) { off = 10; src = 0x5ad6a; }      // $32bd4
    else { off = 14; src = 0x5ad6e; }
    uint32_t add = off == 10 ? 0x5ae22 : 0x5ae26;
    setb(5, rd8(A(0) + 6));
    setw(5, lo(5) & 15);
    setw(2, rd16(A(0) + off + uint32_t(e16(lo(0)))));
    setw(2, asr16(lo(2), D(5)));
    setw(7, rd16(src));
    negw(7);
    setw(6, rd16(add));
    setw(5, rd16(0x5adb0));
    setw(6, asl16(lo(6), D(5)));
    setw(2, lo(2) + lo(6));
    less = int16_t(lo(7)) < int16_t(lo(2));
  }
  if (!less) {                                        // $32c1a
    bool z = !(lo(1) & 0x1000);
    cpu().fz = z;
    cpu().fn = cpu().fv = cpu().fc = false;
    return z;
  }
  if (lo(1) & 0x1000) {                               // $32c2a
    setw(7, 0);
    flags(0, 16);
    return true;
  }
  D(7) = 1;
  flags(1, 32);
  return false;
}

// ---- $328ea: transform the vertex block of the current object -----------------
void s328ea() {
  m68k::Cpu& c = cpu();
  A(3) = 0x5d684;
  c.push32(A(1));
  WW(-10, lo(0));
  A(1) = rd32(0x5ad2a);
  setb(6, rd8(A(1) + 7));
  if (lob(6) & 1) {
    uint32_t sa2 = A(2), sa5 = A(5);
    A(1) += 10 + uint32_t(e16(lo(7)));
    A(2) = 0x5aebe;
    A(4) = A(2);
    A(3) += uint32_t(e16(lo(7)));
    D(0) = RL(-32);
    D(1) = RL(-28);
    D(2) = RL(-24);
    std::swap(D(1), D(2));
    D(3) = rd32(0x5ae28);
    D(4) = rd32(0x5ae2c);
    D(5) = rd32(0x5ae30);
    D(0) += D(3);
    D(2) += D(4);
    D(1) += D(5);
    D(7) = 8;
    setw(7, lo(7) - RW(-6));
    D(0) = uint32_t(asr32(int32_t(D(0)), D(7)));
    D(1) = uint32_t(asr32(int32_t(D(1)), D(7)));
    D(2) = uint32_t(asr32(int32_t(D(2)), D(7)));
    A(0) = uint32_t(e16(lo(2)));
    do {                                              // $3293e
      movemw(A(1), 2, 3);
      A(1) += 6;
      setw(7, RW(-8));
      setw(2, asr16(lo(2), D(7)));
      setw(3, asr16(lo(3), D(7)));
      setw(4, asr16(lo(4), D(7)));
      A(2) = A(4);
      row3(2, 3, 4, 2);
      A(5) = uint32_t(e16(lo(7)));
      row3(2, 3, 4, 2);
      last3(2);
      if (RB(-127) & 1) {
        s3223e();
      } else {
        setw(2, uint16_t(A(5)));
        setw(3, lo(7));
        setw(2, lo(2) + lo(0));
        setw(3, lo(3) + uint16_t(A(0)));
        setw(4, lo(4) + lo(1));
        A(2) = A(4) + 18;
        rot_out(2);
      }
      WW(-10, RW(-10) - 1);
    } while (int16_t(RW(-10)) > 0);
    A(2) = sa2;
    A(5) = sa5;
    A(1) = c.pop32();
    return;
  }
  A(1) += 10;                                         // $329e2
  A(1) += uint32_t(e16(lo(7)));
  A(0) = 0x5aed0;
  A(4) = A(0);
  D(0) = RL(-32);
  D(1) = RL(-28);
  D(2) = RL(-24);
  std::swap(D(1), D(2));
  D(0) += rd32(0x5ae28);
  D(1) += rd32(0x5ae30);
  D(3) = 8;
  setw(3, lo(3) - RW(-6));
  D(0) = uint32_t(asr32(int32_t(D(0)), D(3)));
  D(1) = uint32_t(asr32(int32_t(D(1)), D(3)));
  D(2) = uint32_t(asr32(int32_t(D(2)), D(3)));
  WW(-18, lo(2));
  setw(6, lo(6) & 2);
  if (lo(6) == 0) {
    A(3) += uint32_t(e16(lo(7)));
    do {                                              // $32a1c
      movemw(A(1), 2, 3);
      A(1) += 6;
      setw(7, RW(-8));
      setw(2, asr16(lo(2), D(7)));
      setw(3, asr16(lo(3), D(7)));
      setw(4, asr16(lo(4), D(7)));
      setw(2, lo(2) + lo(0));
      setw(3, lo(3) + RW(-18));
      setw(4, lo(4) + lo(1));
      A(0) = A(4);
      rot_out(0);
      WW(-10, RW(-10) - 1);
    } while (int16_t(RW(-10)) > 0);
    A(1) = c.pop32();
    return;
  }
  setw(2, lo(7));                                     // $32a74
  setw(2, asr16(lo(2), 1));
  setw(7, lo(7) + lo(2));
  A(3) += uint32_t(e16(lo(7)));
  movemw(FP() - 120 + 0, 3, 1);                       // movemw fp@(-120),d3/d6
  D(6) = uint32_t(e16(RW(-118)));
  do {                                                // $32a82
    setw(2, rd16(A(1)));
    A(1) += 2;
    setw(4, rd16(A(1)));
    A(1) += 2;
    setw(7, RW(-8));
    setw(2, asr16(lo(2), D(7)));
    setw(4, asr16(lo(4), D(7)));
    setw(2, lo(2) + lo(0));
    setw(4, lo(4) + lo(1));
    A(0) = A(4);
    row_xz(0, 3);
    {
      setw(5, lo(2));
      setw(7, lo(4));
      D(5) = mm(5, 0);
      A(0) += 2;
      D(7) = mm(7, 0);
      D(7) += D(5);
      D(7) = uint32_t(asr32(int32_t(D(7)), 8));
      setw(7, lo(7) + lo(6));
      wr16(A(3), lo(7));
      A(3) += 2;
    }
    last_xz(0, RW(-116));
    WW(-10, RW(-10) - 1);
  } while (int16_t(RW(-10)) > 0);
  A(1) = c.pop32();
}

// ---- $32f6e: transform vertex block (distant objects) --------------------------
void s32f6e() {
  A(3) = 0x5d684;
  setw(2, lo(7));
  setw(2, asr16(lo(2), 1));
  setw(2, lo(2) + lo(7));
  A(3) += uint32_t(e16(lo(2)));
  WW(-10, lo(0));
  A(1) = rd32(0x5ad2a);
  A(1) += 6;
  A(1) += uint32_t(e16(lo(7)));
  A(0) = 0x5aed0;
  A(4) = A(0);
  setw(0, RW(-134));
  setw(1, RW(-130));
  setw(0, lo(0) + rd16(0x5ae22));
  setw(1, lo(1) + rd16(0x5ae26));
  D(3) = uint32_t(e16(RW(-120)));
  D(6) = uint32_t(e16(RW(-118)));
  do {                                                // $32fac
    setw(2, rd16(A(1)));
    A(1) += 2;
    setw(4, rd16(A(1)));
    A(1) += 2;
    setw(7, RW(-8));
    setw(2, asr16(lo(2), D(7)));
    setw(4, asr16(lo(4), D(7)));
    setw(2, lo(2) + lo(0));
    setw(4, lo(4) + lo(1));
    A(0) = A(4);
    row_xz(0, 3);
    {
      setw(5, lo(2));
      setw(7, lo(4));
      D(5) = mm(5, 0);
      A(0) += 2;
      D(7) = mm(7, 0);
      D(7) += D(5);
      D(7) = uint32_t(asr32(int32_t(D(7)), 8));
      setw(7, lo(7) + lo(6));
      wr16(A(3), lo(7));
      A(3) += 2;
    }
    last_xz(0, RW(-116));
    WW(-10, RW(-10) - 1);
  } while (int16_t(RW(-10)) > 0);
}

// ---- polygon-list ops dispatched through $33478 -----------------------------------
void op_32ffa() {
  setw(7, rd16(A(2)));
  A(2) += 2;
  A(0) = 0x61284;
  wr16(A(0), 0);
  A(0) += 2;
  A(3) = 0x5d684;
  wr16(A(0), lo(7));
  A(0) += 2;
  setw(7, lo(7) - 3);
  setw(1, rd16(A(2)));
  A(2) += 2;
  A(4) = A(3) + uint32_t(e16(lo(1)));
  wr32(A(0), rd32(A(4)));
  A(0) += 4;
  A(4) += 4;
  setw(6, rd16(A(4)));
  wr16(A(0), lo(6));
  A(0) += 2;
  setw(1, rd16(A(2)));
  A(2) += 2;
  A(4) = A(3) + uint32_t(e16(lo(1)));
  wr32(A(0), rd32(A(4)));
  A(0) += 4;
  A(4) += 4;
  wr16(A(0), rd16(A(4)));
  A(0) += 2;
  setw(6, lo(6) & rd16(A(4)));
  do {                                                // $33028
    setw(1, rd16(A(2)));
    A(2) += 2;
    A(4) = A(3) + uint32_t(e16(lo(1)));
    wr32(A(0), rd32(A(4)));
    A(0) += 4;
    A(4) += 4;
    wr16(A(0), rd16(A(4)));
    A(0) += 2;
    setw(6, lo(6) & rd16(A(4)));
    setw(7, lo(7) - 1);
  } while (int16_t(lo(7)) >= 0);
  wr16(0x5ac4c, rd16(A(2)));
  A(2) += 2;
  if (int16_t(lo(6)) < 0) {
    D(0) = 0;
    flags(0, 32);
    return;
  }
  cpu().push32(A(2));
  call_c(0x38c0a);
  A(2) = cpu().pop32();
}
void op_33052() {
  setw(7, rd16(A(2)));
  A(2) += 2;
  A(0) = 0x61284;
  wr16(A(0), 0);
  A(0) += 2;
  A(3) = 0x5d684;
  wr16(A(0), lo(7));
  A(0) += 2;
  setw(7, lo(7) - 4);
  wr32(A(0), rd32(A(3)));
  A(0) += 4;
  A(3) += 4;
  setw(6, rd16(A(3)));
  A(3) += 2;
  wr16(A(0), lo(6));
  A(0) += 2;
  for (int i = 0; i < 2; ++i) {
    wr32(A(0), rd32(A(3)));
    A(0) += 4;
    A(3) += 4;
    wr16(A(0), rd16(A(3)));
    A(0) += 2;
    setw(6, lo(6) & rd16(A(3)));
    A(3) += 2;
  }
  do {                                                // $3307a
    wr32(A(0), rd32(A(3)));
    A(0) += 4;
    A(3) += 4;
    wr16(A(0), rd16(A(3)));
    A(0) += 2;
    setw(6, lo(6) & rd16(A(3)));
    A(3) += 2;
    setw(7, lo(7) - 1);
  } while (lo(7) != 0xFFFF);
  wr16(0x5ac4c, rd16(A(2)));
  A(2) += 2;
  if (int16_t(lo(6)) < 0) {
    D(0) = 0;
    flags(0, 32);
    return;
  }
  cpu().push32(A(2));
  call_c(0x38c0a);
  A(2) = cpu().pop32();
}
void op_3309e() {
  setw(7, rd16(A(2)));
  A(2) += 2;
  A(0) = 0x61284;
  wr16(A(0), 0);
  A(0) += 2;
  A(3) = 0x5d684;
  A(3) += uint32_t(e16(rd16(A(2))));
  A(2) += 2;
  wr16(A(0), lo(7));
  A(0) += 2;
  setw(7, lo(7) - 3);
  wr32(A(0), rd32(A(3)));
  A(0) += 4;
  A(3) += 4;
  setw(6, rd16(A(3)));
  A(3) += 2;
  wr16(A(0), lo(6));
  A(0) += 2;
  for (int i = 0; i < 2; ++i) {
    wr32(A(0), rd32(A(3)));
    A(0) += 4;
    A(3) += 4;
    wr16(A(0), rd16(A(3)));
    A(0) += 2;
    setw(6, lo(6) & rd16(A(3)));
    A(3) += 2;
  }
  do {                                                // $330c6
    wr32(A(0), rd32(A(3)));
    A(0) += 4;
    A(3) += 4;
    wr16(A(0), rd16(A(3)));
    A(0) += 2;
    setw(6, lo(6) & rd16(A(3)));
    A(3) += 2;
    setw(7, lo(7) - 1);
  } while (lo(7) != 0xFFFF);
  if (int16_t(lo(6)) < 0) {
    D(0) = 0;
    flags(0, 32);
    return;
  }
  wr16(0x5ac4c, 7);                                   // $330e8
  setw(1, 3);
  setw(2, 3);
  wr16(0x5a9de, lo(1));
  wr16(0x5a9e0, lo(2));
  cpu().push32(A(2));
  call_c(0x38c0a);
  A(2) = cpu().pop32();
  setw(1, 15);
  setw(2, 0xFFFF);
  wr16(0x5a9de, lo(1));
  wr16(0x5a9e0, lo(2));
}
NATIVE_ASM(0x32ffa, op_32ffa, true);
NATIVE_ASM(0x33052, op_33052, true);
NATIVE_ASM(0x3309e, op_3309e, true);

// ---- $33120 / $33140 -------------------------------------------------------------
void sky_ground_33120() {
  A(0) = 0x331f0;
  call_c(0x33160);
  bool z = cpu().fz;
  wr8(0x5ab93, z ? 0 : 1);
  flags(z ? 0 : 1, 8);
}
void sky_ground_33140() {
  A(0) = 0x33420;
  call_c(0x33160);
  bool z = cpu().fz;
  wr8(0x5ab94, z ? 0 : 1);
  flags(z ? 0 : 1, 8);
}
NATIVE_ASM(0x33120, sky_ground_33120, true);
NATIVE_ASM(0x33140, sky_ground_33140, true);

// ---- $33160 -----------------------------------------------------------------------
// Table lookup in the list at a0: key lists for ac42 and ac40, then the
// entries whose ad70 range and ab48/ab4c byte lists match. d0 = 1/0.
void table_match_33160() {
  setw(0, rd16(0x5ac40));
  setw(1, rd16(0x5ac42));
  A(1) = A(0);
  auto scan = [](uint16_t key) {                      // $33170 / $33188
    D(3) = 0xFFFFFFFEu;
    for (;;) {
      setw(3, lo(3) + 2);
      setw(2, rd16(A(0)));
      A(0) += 2;
      if (int16_t(lo(2)) < 0) return false;
      if (key == lo(2)) break;
    }
    do {
      A(0) += 2;
    } while (int16_t(rd16(A(0) - 2)) >= 0);
    setw(3, rd16(A(0) + uint32_t(ext(lo(3)))));
    A(0) = A(1) + uint32_t(ext(lo(3)));
    return true;
  };
  if (!scan(lo(1)) || !scan(lo(0))) {
    D(0) = 0;
    flags(0, 32);
    return;
  }
  D(4) = uint32_t(-int32_t(rd32(0x5ad70)));
L331a6:
  if (int16_t(rd16(A(0))) < 0) goto fail;
  {
    uint16_t w = rd16(A(0));
    A(0) += 2;
    int32_t v = int32_t(rd32(A(0)));
    A(0) += 4;
    if (w != 0) {
      if (int32_t(D(4)) < v) goto fail;
    } else if (int32_t(D(4)) > v) {
      goto fail;
    }
  }
  setb(6, rd8(0x5ab48));                              // $331ba
L331c0:
  setb(5, rd8(A(0)));
  A(0) += 1;
  if (int8_t(lob(5)) < 0) {                           // $331ce
    setw(5, A(0));
    if (lo(5) & 1) A(0) += 1;
    goto L331a6;
  }
  if (lob(6) == lob(5)) {                             // $331da
    setb(6, rd8(0x5ab4c));
    for (;;) {
      setb(5, rd8(A(0)));
      A(0) += 1;
      if (int8_t(lob(5)) < 0) goto L331c0;
      if (lob(6) == lob(5)) {
        D(0) = 1;
        flags(1, 32);
        return;
      }
    }
  }
  do {
    A(0) += 1;
  } while (int8_t(rd8(A(0) - 1)) >= 0);
  goto L331c0;
fail:
  D(0) = 0;
  flags(0, 32);
}
NATIVE_ASM(0x33160, table_match_33160, true);

// ---- $31c50 -----------------------------------------------------------------------
void view_offset_31c50() {
  D(1) = 0;
  setw(1, rd16(0x5ac44));
  setw(1, lo(1) & 255);
  setw(1, lo(1) - RW(-28));
  swapw(1);
  D(1) = uint32_t(asr32(int32_t(D(1)), 2));
  D(2) += D(1);
  D(1) = 0;
  setw(1, rd16(0x5ac46));
  setw(1, lo(1) & 255);
  setw(1, lo(1) - RW(-30));
  swapw(1);
  D(1) = uint32_t(asr32(int32_t(D(1)), 2));
  D(4) += D(1);
}
NATIVE_ASM(0x31c50, view_offset_31c50, true);

// ---- the per-object body ($31d6c ...) and the draw-op interpreter ($32648) ----------
// One goto web with the original addresses as labels.  Entered with the
// frame already linked and the flag word at -128 initialised.
void render_body() {
  // $31d6c
  setw(1, rd16(0x5ae38));
  WW(-150, lo(1));
  if (rd8(0x5aad5) == 0) {
    setw(3, rd16(0x5ad3a));
    extl(3);
    D(3) = uint32_t(asl32(int32_t(D(3)), 8));
    divuw(D(3), 128);
    D(1) = uint32_t(lo(3)) * lo(1);
    D(1) = uint32_t(asr32(int32_t(D(1)), 8));
  }
  WW(-40, lo(1));
  A(2) = rd32(0x5ad2e);
  WL(-44, A(0));
  setw(3, rd16(0x5adb0));
L31da4:
  setw(0, rd16(A(2)));
  A(2) += 2;
  if (int16_t(lo(0)) < 0) goto L31dd4;
  setw(2, lo(0));
  setw(0, lo(0) & 16383);
  setw(0, asr16(lo(0), D(3)));
  if (int16_t(lo(0)) > int16_t(lo(1))) goto L31dca;
  setw(2, lo(2) & 0x4000);
  if (lo(2) == 0) goto L31dbe;
  A(2) += 4;
  goto L31da4;
L31dbe:
  setw(0, rd16(A(2)));
  if (int16_t(lo(0)) < 0) goto L31df0;
  A(2) = RL(-44);
  A(2) += uint32_t(e16(lo(0)));
  goto L31da4;
L31dca:
  setw(2, lo(2) & 0x4000);
  if (lo(2) == 0) A(2) += 2;
  setw(0, rd16(A(2)));
  A(2) += 2;
L31dd4:
  if (lo(0) == 0xFFFF) goto L31df0;
  setw(1, lo(0));
  setw(1, lo(1) & 0x2000);
  if (lo(1) == 0) goto L31df6;
  if (rd8(0x5ab5c) == 0) goto L31e2a;
L31dea:
  D(0) = 1;
  leave_frame();
  flags(1, 32);
  return;
L31df0:
  ret0_leave();
  return;
L31df6:
  if (int16_t(rd16(0x5ad3a)) >= 128) goto L31e22;
  if (rd8(0x5ab5c) != 0) goto L31e2a;
  A(3) = 0x649ba;
  for (;;) {
    setw(1, rd16(A(3)));
    if (int16_t(lo(1)) < 0) goto L31e2a;
    setw(1, lo(1) >> 8);
    if (lo(1) == rd16(0x5acac)) goto L31dea;
    A(3) += 24;
  }
L31e22:
  if (rd8(0x5ab5c) == 0) goto L31dea;
L31e2a:
  setw(1, lo(0));
  D(7) = 0;
  setw(1, lo(1) & 0x4000);
  if (lo(1) != 0) goto L31e4a;
  D(7) = 1;
  if (rd8(0x5aa96) == 0) goto L31e4a;
  setw(4, rd16(0x5abd2));
  setw(4, lo(4) & 1);
  if (lo(4) == 0) goto L31df0;
L31e4a:
  WW(-98, lo(7));
  setw(0, lo(0) & 4095);
  A(3) = RL(-44);
  A(1) = A(3) + uint32_t(e16(lo(0)));
  wr32(0x5ad2a, A(1));
  wr32(0x5ad2e, A(2));
  setw(0, rd16(A(1)));
  A(1) += 2;
  WB(-136, lob(0));
  setw(4, rd16(A(1)));
  A(1) += 2;
  setw(5, rd16(A(1)));
  A(1) += 2;
  setb(6, rd8(A(1)));
  A(1) += 1;
  setb(7, lob(6));
  setb(6, uint8_t(int8_t(lob(6)) >> 4));
  setw(6, lo(6) & 15);
  setw(7, lo(7) & 15);
  D(0) = rd32(0x5ad5a);
  D(1) = rd32(0x5ad5e);
  D(2) = rd32(0x5ad62);
  setw(3, rd16(0x5adb0));
  if (lo(3) != 0) {
    D(0) = uint32_t(asr32(int32_t(D(0)), D(3)));
    D(1) = uint32_t(asr32(int32_t(D(1)), D(3)));
    D(2) = uint32_t(asr32(int32_t(D(2)), D(3)));
  }
  WL(-32, D(0));
  WL(-28, D(1));
  WL(-24, D(2));
  D(0) = uint32_t(asr32(int32_t(D(0)), 8));
  D(1) = uint32_t(asr32(int32_t(D(1)), 8));
  D(2) = uint32_t(asr32(int32_t(D(2)), 8));
  WW(-134, lo(0));
  WW(-132, lo(1));
  WW(-130, lo(2));
  negw(0);
  negw(1);
  negw(2);
  WW(-38, lo(0));
  WW(-36, lo(1));
  WW(-34, lo(2));
  WW(-4, lo(7));
  setw(5, asr16(lo(5), D(3)));
  if (int16_t(lo(5)) >= int16_t(RW(-150))) goto L31ec4;
  setw(3, lo(7));
  setw(2, 0);
  goto L31ede;
L31ec4:
  setw(3, rd16(0x5adb0));
  setw(4, asr16(lo(4), D(3)));
  if (int16_t(lo(4)) >= int16_t(RW(-150))) goto L31eda;
  setw(3, lo(6));
  setw(2, lo(7));
  setw(2, lo(2) - lo(6));
  goto L31ede;
L31eda:
  setw(3, 0);
  setw(2, lo(7));
L31ede:
  WW(-6, lo(2));
  setw(3, lo(3) + rd16(0x5adb0));
  WW(-8, lo(3));
  setb(6, rd8(A(1)));
  A(1) += 1;
  WB(-100, lob(6));
  if (sl(0x5ad70) <= -320) goto L31f36;
  if (rd8(0x5ab64) != 0) goto L31f36;
  setb(6, lob(6) & 66);
  if (lob(6) != 66) goto L31f36;
  if (int16_t(rd16(0x5adb2)) > 0) {
    setb(0, rd8(0x5abb5) & 3);
    if (lob(0) != 0) goto L31f36;
  }
  A(3) = A(1) + 2;
  call_c(0x33fda);
  if (lt_flags()) goto L31df0;
L31f36:
  wr16(0x61284, 0);
  A(3) = 0x5d684;
  setb(6, RB(-100));
  if (!(lob(6) & 1)) goto L323b4;
  A(0) = scenario_base + uint32_t(e16(rd16(0x5acae)));
  setw(1, rd16(A(0)));
  if (!(lo(1) & 0x40)) goto L31df0;
  wr8(A(0) + 123, RB(-136));
  setw(1, lo(1) & 1024);
  if (lo(1) == 0) goto L31fc8;
  setw(1, rd16(A(0) + 76));
  if (lo(1) != 14) goto L31fc0;
  setw(0, RW(-40));
  extl(0);
  setw(2, rd16(0x5adb0));
  D(0) = uint32_t(asl32(int32_t(D(0)), D(2)));
  D(0) = uint32_t(asr32(int32_t(D(0)), 1));
  if (int16_t(lo(0)) <= 0) goto L31fa2;
  D(2) = 65536;
  divuw(D(2), lo(0));
  if (int32_t(D(2)) <= 120) goto L31fa6;
L31fa2:
  setw(2, 120);
L31fa6:
  wr8(0x5ab61, lob(2));
  setw(2, asr16(lo(2), 1));
  wr8(0x5ab62, lob(2));
  setw(2, asr16(lo(2), 4));
  D(4) = 4;
  setw(4, lo(4) - lo(2));
  wr8(0x5aab0, lob(4));
L31fc0:
  if (int16_t(lo(1)) <= 12) goto L327ca;
L31fc8:
  D(0) = RL(-32);
  D(2) = RL(-24);
  D(3) = rd32(0x5ae28);
  D(4) = rd32(0x5ae30);
  D(0) += D(3);
  D(2) += D(4);
  D(1) = rd32(0x5ae34);
  D(7) = 8;
  setw(7, lo(7) - RW(-6));
  D(0) = uint32_t(asr32(int32_t(D(0)), D(7)));
  D(1) = uint32_t(asr32(int32_t(D(1)), D(7)));
  D(2) = uint32_t(asr32(int32_t(D(2)), D(7)));
  WW(-20, lo(0));
  WW(-18, lo(1));
  WW(-16, lo(2));
  A(0) = scenario_base + uint32_t(e16(rd16(0x5acae)));
  A(0) += 146;
  A(2) = 0x5aebe;
  movemw(A(0), 0, 8);
  A(0) += 16;
  for (int i = 0; i < 8; ++i) setw(i, asr16(lo(i), 6));
  for (int i = 0; i < 8; ++i) wr16(A(2) + 2u * uint32_t(i), lo(i));
  setw(0, rd16(A(0)));
  setw(0, asr16(lo(0), 6));
  wr16(A(2) + 16, lo(0));
  A(0) = scenario_base + uint32_t(e16(rd16(0x5acae)));
  A(0) += 164;
  setb(0, rd8(A(1)));
  A(1) += 1;
  extw(0);
  setb(1, rd8(A(1)));
  A(1) += 1;
  extw(1);
  setw(0, lo(0) - 1);
  setw(1, lo(1) - 1);
  {
    int32_t r = int32_t(int16_t(lo(0))) - int16_t(lo(1));
    setw(0, uint32_t(r));
    if (r < 0) error_loop(23);
  }
  WW(-96, 57);
L32056:
  movemw(A(1), 2, 3);
  A(1) += 6;
  A(2) = 0x5aebe;
  row3(2, 3, 4, 2);
  A(4) = uint32_t(e16(lo(7)));
  row3(2, 3, 4, 2);
  setw(6, lo(7));
  last3(2);
  wr16(A(0), uint16_t(A(4)));
  A(0) += 2;
  wr16(A(0), lo(7));
  A(0) += 2;
  wr16(A(0), lo(4));
  A(0) += 2;
  if (int16_t(lo(1)) < 0) goto L32234;
  if (RB(-127) & 1) {
    s3223e();
    goto L3213e;
  }
  setw(2, uint16_t(A(4)));
  setw(3, lo(6));
  setw(7, RW(-8));
  setw(2, asr16(lo(2), D(7)));
  setw(3, asr16(lo(3), D(7)));
  setw(4, asr16(lo(4), D(7)));
  setw(2, lo(2) + RW(-20));
  setw(3, lo(3) + RW(-18));
  setw(4, lo(4) + RW(-16));
  A(2) = 0x5aed0;
  rot_out(2);
  setw(2, rd16(0x5acb8));
  if (int16_t(lo(2)) < 0) goto L3213e;
  if (lo(2) != rd16(0x5acae)) goto L3213e;
  {
    uint16_t s0 = lo(0), s1 = lo(1), s4 = lo(4), s7 = lo(7);
    movemw(A(3) - 6, 0, 3);
    call_c(0x4389c);
    setw(0, lo(0) - rd16(0x5ac80));
    setw(1, lo(1) - rd16(0x5abd0));
    wr16(0x5ac2e, lo(0));
    wr16(0x5ac30, lo(1));
    D(0) = uint32_t(e16(s0));
    D(1) = uint32_t(e16(s1));
    D(4) = uint32_t(e16(s4));
    D(7) = uint32_t(e16(s7));
  }
L3213e:
  if (int16_t(RW(-98)) <= 1) goto L3216c;
  setw(2, lo(4));
  if (int16_t(lo(2)) <= 0) goto L32166;
  setw(2, asr16(lo(2), 1));
  setw(4, lo(4) + lo(2));
  setw(2, rd16(A(3) - 6));
  if (int16_t(lo(2)) > int16_t(lo(4))) goto L32166;
  negw(2);
  if (int16_t(lo(2)) > int16_t(lo(4))) goto L32166;
  if (int16_t(lo(7)) > int16_t(lo(4))) goto L32166;
  negw(7);
  if (int16_t(lo(7)) <= int16_t(lo(4))) goto L3216c;
L32166:
  ret0_leave();
  return;
L3216c:
  movemw(A(1), 2, 3);
  A(1) += 6;
  A(2) = 0x5aebe;
  row3(2, 3, 4, 2);
  A(4) = uint32_t(e16(lo(7)));
  row3(2, 3, 4, 2);
  setw(6, lo(7));
  last3(2);
  setw(3, lo(4));
  WW(-96, RW(-96) - 1);
  if (int16_t(RW(-96)) >= 0) {
    wr16(A(0), uint16_t(A(4)));
    A(0) += 2;
    wr16(A(0), lo(7));
    A(0) += 2;
    wr16(A(0), lo(3));
    A(0) += 2;
  }
  setw(1, lo(1) - 1);
  if (int16_t(lo(1)) < 0) goto L32234;
  if (RB(-127) & 1) {
    s3223e();
    goto L3216c;
  }
  setw(2, uint16_t(A(4)));
  setw(3, lo(6));
  setw(7, RW(-8));
  setw(2, asr16(lo(2), D(7)));
  setw(3, asr16(lo(3), D(7)));
  setw(4, asr16(lo(4), D(7)));
  setw(2, lo(2) + RW(-20));
  setw(3, lo(3) + RW(-18));
  setw(4, lo(4) + RW(-16));
  A(2) = 0x5aed0;
  rot_out(2);
  goto L3216c;
L32234:
  {
    int r = int(int16_t(lo(0))) - 1;
    setw(0, uint32_t(r));
    if (r > 0) goto L3216c;
  }
  goto L32648;

L323b4:
  setb(3, rd8(A(1)));
  A(1) += 1;
  extw(3);
  A(1) += 1;
  A(2) = 0x5aed0;
  A(4) = A(2);
  D(0) = RL(-32);
  D(1) = RL(-28);
  D(2) = RL(-24);
  std::swap(D(1), D(2));
  D(0) += rd32(0x5ae28);
  D(1) += rd32(0x5ae30);
  D(2) += rd32(0x5ae2c);
  D(7) = 8;
  setw(7, lo(7) - RW(-6));
  D(0) = uint32_t(asr32(int32_t(D(0)), D(7)));
  D(1) = uint32_t(asr32(int32_t(D(1)), D(7)));
  D(2) = uint32_t(asr32(int32_t(D(2)), D(7)));
  A(5) = uint32_t(e16(lo(2)));
  A(0) = FP() - 10;
  setw(3, lo(3) - 1);
  wr16(A(0), lo(3));
  setw(7, lo(6));
  setw(6, lo(6) & 2);
  if (lo(6) != 0) goto L3229a;
  // plain vertices
  for (;;) {
    movemw(A(1), 2, 3);
    A(1) += 6;
    setw(7, RW(-8));
    setw(2, asr16(lo(2), D(7)));
    setw(3, asr16(lo(3), D(7)));
    setw(4, asr16(lo(4), D(7)));
    setw(2, lo(2) + lo(0));
    setw(3, lo(3) + uint16_t(A(5)));
    setw(4, lo(4) + lo(1));
    A(2) = A(4);
    rot_out(2);
    break;
  }
  if (RW(-98) != 0) {
    setw(2, lo(4));
    if (int16_t(lo(2)) <= 0) goto L3246e;
    setw(2, asr16(lo(2), 1));
    setw(4, lo(4) + lo(2));
    setw(2, rd16(A(3) - 6));
    if (int16_t(lo(2)) > int16_t(lo(4))) goto L3246e;
    negw(2);
    if (int16_t(lo(2)) > int16_t(lo(4))) goto L3246e;
    if (int16_t(lo(7)) > int16_t(lo(4))) goto L3246e;
    negw(7);
    if (int16_t(lo(7)) > int16_t(lo(4))) goto L3246e;
  }
  if (int16_t(rd16(A(0))) > 0) {
    do {
      movemw(A(1), 2, 3);
      A(1) += 6;
      setw(7, RW(-8));
      setw(2, asr16(lo(2), D(7)));
      setw(3, asr16(lo(3), D(7)));
      setw(4, asr16(lo(4), D(7)));
      setw(2, lo(2) + lo(0));
      setw(3, lo(3) + uint16_t(A(5)));
      setw(4, lo(4) + lo(1));
      A(2) = A(4);
      rot_out(2);
      wr16(A(0), rd16(A(0)) - 1);
    } while (sw(A(0)) > 0);
  }
  setb(4, RB(-100));
  setb(4, lob(4) & 8);
  if (lob(4) == 0) goto L32648;
  A(0) = 0x60084;
  goto L324da;
L3246e:
  ret0_leave();
  return;
L324da:
  WW(-48, rd16(A(1)));
  A(1) += 2;
  if (int16_t(RW(-48)) < 0) goto L32648;
  WW(-10, 12);
L324e8:
  movemw(A(1), 2, 3);
  A(1) += 6;
  setw(7, RW(-8));
  setw(2, asr16(lo(2), D(7)));
  setw(3, asr16(lo(3), D(7)));
  setw(4, asr16(lo(4), D(7)));
  setw(2, lo(2) + lo(0));
  setw(3, lo(3) + uint16_t(A(5)));
  setw(4, lo(4) + lo(1));
  A(2) = 0x5aed0;
  for (uint32_t k = 0; k < 2; ++k) {                  // first two components: long to (a0), word to (a3)
    setw(5, lo(2));
    setw(6, lo(3));
    setw(7, lo(4));
    D(5) = mm(5, 2);
    D(6) = mm(6, 2);
    D(7) = mm(7, 2);
    D(7) += D(6);
    D(7) += D(5);
    wr32(A(0) + 4 * k, D(7));
    D(7) = uint32_t(asr32(int32_t(D(7)), 8));
    wr16(A(3), lo(7));
    A(3) += 2;
  }
  D(2) = mm(2, 2);
  D(3) = mm(3, 2);
  D(4) = mm(4, 2);
  D(4) += D(3);
  D(4) += D(2);
  wr32(A(0) + 8, D(4));
  D(4) = uint32_t(asr32(int32_t(D(4)), 8));
  wr16(A(3), lo(4));
  A(3) += 2;
  A(3) += 6;
L32546:
  movemw(A(1), 2, 3);
  A(1) += 6;
  setw(7, RW(-8));
  setw(2, asr16(lo(2), D(7)));
  setw(3, asr16(lo(3), D(7)));
  setw(4, asr16(lo(4), D(7)));
  setw(2, lo(2) + lo(0));
  setw(3, lo(3) + uint16_t(A(5)));
  setw(4, lo(4) + lo(1));
  A(2) = 0x5aed0;
  for (uint32_t k = 0; k < 2; ++k) {
    setw(5, lo(2));
    setw(6, lo(3));
    setw(7, lo(4));
    D(5) = mm(5, 2);
    D(6) = mm(6, 2);
    D(7) = mm(7, 2);
    D(7) += D(6);
    D(7) += D(5);
    wr32(A(0) + 12 + 4 * k, D(7));
  }
  D(2) = mm(2, 2);
  D(3) = mm(3, 2);
  D(4) = mm(4, 2);
  D(4) += D(3);
  D(4) += D(2);
  wr32(A(0) + 20, D(4));
  {
    uint16_t s0 = lo(0), s1 = lo(1);                  // movemw d0-d1,-(sp)
    setw(0, rd16(A(1)));
    A(1) += 2;
    if (int16_t(lo(0)) > 1) {
      for (int i = 0; i < 6; ++i) D(2 + i) = rd32(A(0) + 4u * uint32_t(i));
      for (int i = 2; i < 8; ++i) D(i) = uint32_t(asr32(int32_t(D(i)), 4));
      D(5) -= D(2);
      D(6) -= D(3);
      D(7) -= D(4);
      for (int r = 5; r <= 7; ++r) {                  // divsw d0,dr
        int64_t q = int64_t(int32_t(D(r))) / int16_t(lo(0));
        int64_t rem = int64_t(int32_t(D(r))) % int16_t(lo(0));
        if (q >= -32768 && q <= 32767) D(r) = (uint32_t(uint16_t(rem)) << 16) | uint16_t(q);
      }
      extl(5);
      extl(6);
      extl(7);
      setw(0, lo(0) - 2);
      do {                                            // $325c6
        D(2) += D(5);
        D(3) += D(6);
        D(4) += D(7);
        D(1) = uint32_t(asr32(int32_t(D(2)), 4));
        wr16(A(3), lo(1));
        D(1) = uint32_t(asr32(int32_t(D(3)), 4));
        wr16(A(3) + 2, lo(1));
        D(1) = uint32_t(asr32(int32_t(D(4)), 4));
        wr16(A(3) + 4, lo(1));
        A(3) += uint32_t(e16(RW(-10)));
        setw(0, lo(0) - 1);
      } while (lo(0) != 0xFFFF);
    }
    D(5) = rd32(A(0) + 12);
    D(6) = rd32(A(0) + 16);
    D(7) = rd32(A(0) + 20);
    wr32(A(0), D(5));
    wr32(A(0) + 4, D(6));
    wr32(A(0) + 8, D(7));
    D(5) = uint32_t(asr32(int32_t(D(5)), 8));
    D(6) = uint32_t(asr32(int32_t(D(6)), 8));
    D(7) = uint32_t(asr32(int32_t(D(7)), 8));
    wr16(A(3), lo(5));
    wr16(A(3) + 2, lo(6));
    wr16(A(3) + 4, lo(7));
    A(3) += uint32_t(e16(RW(-10)));
    D(0) = uint32_t(e16(s0));
    D(1) = uint32_t(e16(s1));
  }
  WW(-48, RW(-48) - 1);
  if (int16_t(RW(-48)) > 0) goto L32546;
  if (int16_t(RW(-10)) < 0) goto L3263c;
  WW(-10, uint16_t(-12));
  WL(-114, A(3));
  setw(2, rd16(A(1)));
  A(1) += 2;
  if (lo(2) == 0) goto L3263c;
  if (lo(2) & 0x8000) {
    A(3) -= 6;
    setw(2, lo(2) & 0x7FFF);
  } else {
    A(3) -= 18;
  }
  WW(-48, lo(2));
  goto L32546;
L3263c:
  if (int16_t(rd16(A(1))) <= 0) goto L32648;
  A(3) = RL(-114);
  goto L324da;

L3229a:
  if (sl(0x5ad70) < -32) goto L322c4;
  setw(7, lo(7) & 4);
  setw(7, lo(7) == 0 ? 96 : 368);
  setw(6, rd16(0x5adb0));
  setw(7, asr16(lo(7), D(6)));
  if (int16_t(lo(7)) < int16_t(RW(-40))) goto L3246e;
L322c4:
  setw(3, uint16_t(A(5)));
  setw(6, lo(3));
  D(3) = uint32_t(int32_t(int16_t(lo(3))) * sw(A(4) + 14));
  D(3) = uint32_t(asr32(int32_t(D(3)), 8));
  A(5) = uint32_t(e16(lo(3)));
  setw(3, lo(6));
  D(3) = uint32_t(int32_t(int16_t(lo(3))) * sw(A(4) + 2));
  D(3) = uint32_t(asr32(int32_t(D(3)), 8));
  D(6) = uint32_t(int32_t(int16_t(lo(6))) * sw(A(4) + 8));
  D(6) = uint32_t(asr32(int32_t(D(6)), 8));
  WW(-120, lo(3));
  WW(-118, lo(6));
  WW(-116, uint16_t(A(5)));
  for (int pass = 0; pass < 2; ++pass) {              // $322e4 (first vertex, with visibility test) then $3235a loop
    if (pass == 1) {
      if (int16_t(rd16(A(0))) <= 0) goto L32648;      // $32354: tstw a0@; blew
    }
    do {
      setw(2, rd16(A(1)));
      A(1) += 2;
      setw(4, rd16(A(1)));
      A(1) += 2;
      setw(7, RW(-8));
      setw(2, asr16(lo(2), D(7)));
      setw(4, asr16(lo(4), D(7)));
      setw(2, lo(2) + lo(0));
      setw(4, lo(4) + lo(1));
      A(2) = A(4);
      row_xz(2, 3);
      row_xz(2, 6);
      last_xz(2, uint16_t(A(5)));
      if (pass == 0) {
        if (RW(-98) != 0) {
          setw(2, lo(4));
          if (int16_t(lo(2)) <= 0) goto L3234e;
          setw(2, asr16(lo(2), 1));
          setw(4, lo(4) + lo(2));
          setw(2, rd16(A(3) - 6));
          if (int16_t(lo(2)) > int16_t(lo(4))) goto L3234e;
          negw(2);
          if (int16_t(lo(2)) > int16_t(lo(4))) goto L3234e;
          if (int16_t(lo(7)) > int16_t(lo(4))) goto L3234e;
          negw(7);
          if (int16_t(lo(7)) > int16_t(lo(4))) goto L3234e;
        }
        break;
      }
      wr16(A(0), rd16(A(0)) - 1);
    } while (sw(A(0)) > 0);
  }
  setb(4, RB(-100));
  setb(4, lob(4) & 4);
  if (lob(4) == 0) goto L32648;
  goto L32c3e;
L3234e:
  ret0_leave();
  return;

L32c3e:
  WW(-48, rd16(A(1)));
  A(1) += 2;
  if (int16_t(RW(-48)) < 0) goto L32648;
  {
    uint16_t s3 = lo(3), s6 = lo(6);
    movemw(A(3) - 12, 2, 6);
    setw(5, lo(5) - lo(2));
    setw(6, lo(6) - lo(3));
    setw(7, lo(7) - lo(4));
    WW(-86 - 2, lo(5));
    WW(-86, lo(6));
    WW(-84, lo(7));
    setw(6, s6);
    setw(3, s3);
  }
  for (;;) {                                          // $32c5e
    setw(2, rd16(A(1)));
    A(1) += 2;
    setw(4, rd16(A(1)));
    A(1) += 2;
    setw(7, RW(-8));
    if (lo(7) != 0) {
      setw(2, asr16(lo(2), D(7)));
      setw(4, asr16(lo(4), D(7)));
    }
    setw(2, lo(2) + lo(0));
    setw(4, lo(4) + lo(1));
    A(2) = A(4);
    row_xz(2, 3);
    row_xz(2, 6);
    last_xz(2, uint16_t(A(5)));
    setw(2, rd16(A(3) - 6));
    std::swap(D(7), D(4));
    setw(2, lo(2) + RW(-88));
    setw(4, lo(4) + RW(-86));
    setw(7, lo(7) + RW(-84));
    wr16(A(3), lo(2));
    wr16(A(3) + 2, lo(4));
    wr16(A(3) + 4, lo(7));
    A(3) += 6;
    WW(-48, RW(-48) - 1);
    if (int16_t(RW(-48)) > 0) continue;
    break;
  }
  {
    int16_t t = sw(A(1));
    A(1) += 2;
    if (t < 0) goto L32648;
  }
  for (int v = 0; v < 2; ++v) {                       // two more explicit vertices
    setw(2, rd16(A(1)));
    A(1) += 2;
    setw(4, rd16(A(1)));
    A(1) += 2;
    setw(7, RW(-8));
    if (lo(7) != 0) {
      setw(2, asr16(lo(2), D(7)));
      setw(4, asr16(lo(4), D(7)));
    }
    setw(2, lo(2) + lo(0));
    setw(4, lo(4) + lo(1));
    A(2) = A(4);
    row_xz(2, 3);
    row_xz(2, 6);
    last_xz(2, uint16_t(A(5)));
  }
  goto L32c3e;

L32648:
  wr32(0x5a9de, 1048575);
  wr32(0x5a9e2, 0);
  A(5) = rd32(0x5ad2e);
  WW(-102, 0);
  WW(-124, 0);
L32666:
  if (RW(-102) != 0) goto L32794;
  setw(0, rd16(A(5)));
  A(5) += 2;
  if (int16_t(lo(0)) < 0) goto L326f0;
  setw(7, rd16(A(5)));
  A(5) += 2;
  setw(1, lo(7));
  setw(1, lo(1) & 0xFC00);
  if (lo(1) == 0xFC00) goto L326de;
  setw(1, lo(1) & 0x0C00);
  {
    bool z;
    if (lo(1) != 0) {
      z = s32b92();
    } else {
      A(3) = 0x5d684;
      setw(1, lo(7));
      setw(1, lo(1) & 0x2000);
      if (lo(1) != 0) {
        movemw(A(5), 0, 6);
        A(5) += 12;
        z = s32aec();
      } else {
        A(0) = 0x61288;
        A(4) = A(3) + uint32_t(e16(lo(0)));
        wr32(A(0), rd32(A(4)));
        A(0) += 4;
        A(4) += 4;
        wr16(A(0), rd16(A(4)));
        A(0) += 2;
        movemw(A(5), 1, 2);
        A(5) += 4;
        A(4) = A(3) + uint32_t(e16(lo(1)));
        wr32(A(0), rd32(A(4)));
        A(0) += 4;
        A(4) += 4;
        wr16(A(0), rd16(A(4)));
        A(0) += 2;
        A(4) = A(3) + uint32_t(e16(lo(2)));
        wr32(A(0), rd32(A(4)));
        A(0) += 4;
        A(4) += 4;
        wr16(A(0), rd16(A(4)));
        A(0) += 2;
        A(3) = uint32_t(e16(lo(7)));
        z = s32adc();
      }
    }
    if (z) goto L326de;
  }
  A(3) = RL(-44);
  A(3) += uint32_t(e16(rd16(A(5) + 2)));
  A(5) = A(3);
  goto L32666;
L326de:
  A(3) = RL(-44);
  A(3) += uint32_t(e16(rd16(A(5))));
  A(5) = A(3);
  goto L32666;
L326f0:
  if (lo(0) == 0xFFFF) {
    ret0_leave();
    return;
  }
  WW(-108, 0);
  WW(-122, 0);
  setw(1, lo(0));
  setw(1, lo(1) & 0x2000);
  if (lo(1) == 0) goto L32722;
  WW(-108, RW(-108) + 1);
  setw(1, lo(0));
  setw(1, lo(1) & 0x1000);
  if (lo(1) == 0) {
    A(3) = RL(-44);
    setw(0, lo(0) & 4095);
    goto L3276a;
  }
  A(2) = rd32(A(5));
  A(5) += 4;
  goto L32856;
L32722:
  setw(1, lo(0));
  setw(1, lo(1) & 0x4000);
  if (lo(1) != 0) WW(-102, RW(-102) + 1);
  setw(1, lo(0));
  setw(1, lo(1) & 0x1000);
  if (lo(1) == 0) {
    A(3) = RL(-44);
    setw(0, lo(0) & 4095);
    A(1) = A(3) + uint32_t(e16(lo(0)));
  } else {
    A(1) = rd32(A(5));
    A(5) += 4;
  }
  WW(-104, 0);
L3274a:
  if (RW(-122) != 0) goto L32666;
  if (RW(-108) != 0) goto L32794;
  if (RW(-104) != 0) goto L32666;
  setw(0, rd16(A(1)));
  A(1) += 2;
  setw(1, lo(0));
  setw(1, lo(1) & 0x8000);
  if (lo(1) != 0) WW(-104, RW(-104) + 1);
L3276a:
  setw(1, lo(0));
  setw(1, lo(1) & 0x1000);
  if (lo(1) == 0) {
    setw(0, lo(0) & 4095);
    A(3) = RL(-44);
    A(2) = A(3) + uint32_t(e16(lo(0)));
  } else {
    A(2) = rd32(A(1));
    A(1) += 4;
  }
L32856:
  WW(-106, 0);
  for (;;) {                                          // $3285a
    if (RW(-106) != 0) goto L3274a;
    setw(0, rd16(A(2)));
    A(2) += 2;
    if (int16_t(lo(0)) < 0) {
      if (lo(0) == 0xFFFF) {
        wr16(0x5ac96, lo(0));
        call_c(0x2f216);
        goto L3274a;
      }
      setw(1, lo(0));
      setw(1, lo(1) & 0x4000);
      if (lo(1) != 0) WW(-106, RW(-106) + 1);
      setw(0, lo(0) & 16383);
      A(0) = 0x33478;
      A(0) = rd32(A(0) + uint32_t(e16(lo(0))));
      call_c(A(0));
      if (lt_flags()) goto L3274a;
      WW(-124, RW(-124) | lo(0));
      continue;
    }
    if (lo(0) != 32767) {                             // $3289e
      setw(1, rd16(0x5adb0));
      setw(0, asr16(lo(0), D(1)));
      if (int16_t(lo(0)) < int16_t(RW(-40))) {        // $32840
        setw(0, rd16(A(2)));
        A(2) += 2;
        if (int16_t(lo(0)) < 0) goto L3274a;
        setw(1, rd16(0x5adb0));
        setw(0, asr16(lo(0), D(1)));
        if (int16_t(lo(0)) < int16_t(RW(-40))) goto L3274a;
      } else {
        int16_t t = sw(A(2));
        A(2) += 2;
        if (t >= 0) goto L3274a;
      }
    }
    setw(0, rd16(A(2)));                              // $328b6
    A(2) += 2;
    if (int16_t(lo(0)) > 0) {
      setw(7, rd16(A(2)));
      A(2) += 2;
      s328ea();
    }
    setw(0, rd16(A(2)));
    A(2) += 2;
    setw(1, lo(0));
    setw(1, lo(1) & 0x4000);
    if (lo(1) != 0) WW(-106, RW(-106) + 1);
    setw(0, lo(0) & 16383);
    A(0) = 0x33478;
    A(0) = rd32(A(0) + uint32_t(e16(lo(0))));
    call_c(A(0));
    if (lt_flags()) goto L3274a;
    WW(-124, RW(-124) | lo(0));
  }
L32794:
  if (rd8(0x5ab53) & 0x40) goto L3278c;
  if (!(rd8(0x5ab53) & 0x10)) goto L3278c;
  A(0) = scenario_base + uint32_t(e16(rd16(0x5acae)));
  setw(0, rd16(A(0)));
  setw(0, lo(0) & 1024);
  if (lo(0) == 0) goto L32828;
  setw(1, rd16(A(0) + 76));
  if (int16_t(lo(1)) < 0) goto L32828;
  if (int16_t(lo(1)) >= 16) goto L32828;
L327ca:
  D(3) = 15;
  setw(3, lo(3) - lo(1));
  if (int16_t(lo(3)) > 15) goto L32828;
  setw(0, rd16(A(0) + 12));
  D(1) = rd32(A(0) + 16);
  setw(2, rd16(A(0) + 14));
  extl(0);
  setw(4, rd16(A(0) + 6));
  setw(4, lo(4) - rd16(0x5ac44));
  extl(4);
  D(5) = 14;
  D(4) = uint32_t(asl32(int32_t(D(4)), D(5)));
  D(0) += D(4);
  extl(2);
  setw(4, rd16(A(0) + 8));
  setw(4, lo(4) - rd16(0x5ac46));
  extl(4);
  D(4) = uint32_t(asl32(int32_t(D(4)), D(5)));
  D(2) += D(4);
  D(4) = 3;
  if (rd8(A(0) + 98) != 21) {
    D(4) = 1;
    if (rd8(A(0) + 4) & 2) D(4) = 2;
  }
  setw(7, rd16(0x5adb0));
  call_c(0x41866);
L32828:
  call_c(0x35884);
L3278c:
  setw(0, RW(-124));
  leave_frame();
  flags(lo(0), 16);
  return;
}

// $31d54/$31d64: link a6,#-152 and run the body.
void render_object(bool picture) {
  cpu().push32(cpu().a[6]);
  cpu().a[6] = cpu().a[7];
  cpu().a[7] -= 152;
  WW(-128, 0);
  if (picture) WW(-128, RW(-128) | 1);
  render_body();
}

void entry_31d64() { render_object(false); }
NATIVE_ASM(0x31d64, entry_31d64, true);

void entry_31c8c() {
  if (sl(0x5ad5e) >= -2621440) {                      // $31c88
    D(0) = 0;
    flags(0, 32);
    return;
  }
  render_object(false);
}
NATIVE_ASM(0x31c8c, entry_31c8c, true);

void entry_31c9c() {
  auto zero = [] {
    D(0) = 0;
    flags(0, 32);
  };
  if (rd8(0x5ab53) & 0x40) { zero(); return; }
  if (rd8(0x5aa7d) != 0) { render_object(false); return; }
  setw(0, rd16(0x5acae));
  if (lo(0) != rd16(0x5abd6)) { render_object(false); return; }
  setb(0, rd8(0x5aa9f));
  if (!(int8_t(lob(0)) < 3)) {
    if (int8_t(lob(0)) <= 9) { render_object(true); return; }
  }
  D(0) = rd32(0x5ad32);                               // $31cd4
  if (D(0) == rd32(0x5ad2e)) { render_object(false); return; }
  wr32(0x5ad2e, D(0));
  if (rd8(0x5ab2d) != 0) { render_object(false); return; }
  setb(1, rd8(0x5ab63));
  if (lob(1) != 0) {
    if (int8_t(lob(1)) >= 3) { render_object(false); return; }
    D(3) = rd32(0x5ad70);
    setw(5, rd16(0x5adb0));
    D(3) = uint32_t(asr32(int32_t(D(3)), D(5)));
    if (int32_t(D(3)) >= -32) { render_object(false); return; }
  }
  A(2) = rd32(0x5ad2e);                               // $31d1c
  setw(0, rd16(A(2)));
  A(2) += 2;
  if (int16_t(lo(0)) < 0) {                           // $31d38
    if (lo(0) == 0xFFFF) { zero(); return; }
    wr32(0x5ad2e, rd32(0x5ad2e) + 4);
    render_object(false);
    return;
  }
  setw(0, lo(0) & 16384);
  if (lo(0) != 0) {
    wr32(0x5ad2e, rd32(0x5ad2e) + 6);
    render_object(false);
    return;
  }
  setw(0, rd16(A(2)));                                // $31d46
  if (int16_t(lo(0)) < 0) { zero(); return; }
  extl(0);
  wr32(0x5ad2e, rd32(0x5ad2e) + D(0));
  render_object(false);
}
NATIVE_ASM(0x31c9c, entry_31c9c, true);

// ---- $32d64 / $32d72: distant-object variant ------------------------------------------
void distant_body() {
  wr32(0x5a9de, 1048575);
  setw(2, rd16(0x5ae38));
  WW(-150, lo(2));
  if (rd8(0x5aad5) == 0) {
    setw(3, rd16(0x5ad3a));
    extl(3);
    D(3) = uint32_t(asl32(int32_t(D(3)), 8));
    divuw(D(3), 128);
    D(2) = uint32_t(lo(3)) * lo(2);
    D(2) = uint32_t(asr32(int32_t(D(2)), 8));
  }
  WW(-40, lo(2));
L32db4:
  A(2) = rd32(0x5ad2e);
  setw(0, rd16(A(2)));
  A(2) += 2;
  if (int16_t(lo(0)) < 0) goto L32dda;
  setw(1, rd16(0x5adb0));
  setw(0, asr16(lo(0), D(1)));
  if (int16_t(lo(0)) >= int16_t(lo(2))) goto L32dd8;
  {
    uint32_t v = rd32(A(2));
    wr32(0x5ad2e, v);
    flags(v, 32);
    if (int32_t(v) >= 0) goto L32db4;
    D(0) = 0;
    leave_frame();
    flags(0, 32);
    return;
  }
L32dd8:
  A(2) += 4;
L32dda:
  if (lo(0) == 0xFFFF) {
    ret0_leave();
    return;
  }
  A(1) = rd32(A(2));
  A(2) += 4;
  wr32(0x5ad2a, A(1));
  wr32(0x5ad2e, A(2));
  setw(0, rd16(0x5ad6a));
  D(1) = rd32(0x5ad5e);
  D(1) = uint32_t(asr32(int32_t(D(1)), 8));
  setw(2, rd16(0x5ad6e));
  setb(7, rd8(0x5ab53) & 15);
  extw(7);
  WW(-8, lo(7));
  if (lo(7) != 0) {
    setw(0, asr16(lo(0), D(7)));
    D(1) = uint32_t(asr32(int32_t(D(1)), D(7)));
    setw(2, asr16(lo(2), D(7)));
  }
  WW(-134, lo(0));
  WW(-132, lo(1));
  WW(-130, lo(2));
  wr16(0x61284, 0);
  A(3) = 0x5d684;
  A(2) = 0x5aed0;
  A(4) = A(2);
  setw(0, rd16(0x5ae22));
  setw(1, rd16(0x5ae26));
  setw(0, lo(0) + RW(-134));
  setw(1, lo(1) + RW(-130));
  A(0) = FP() - 10;
  wr16(A(0), rd16(A(1)));
  A(1) += 2;
  A(1) += 4;
  setw(2, RW(-132));
  setw(3, lo(2));
  setw(6, lo(2));
  D(2) = uint32_t(int32_t(int16_t(lo(2))) * sw(A(4) + 14));
  D(2) = uint32_t(asr32(int32_t(D(2)), 8));
  A(5) = uint32_t(e16(lo(2)));
  D(3) = uint32_t(int32_t(int16_t(lo(3))) * sw(A(4) + 2));
  D(3) = uint32_t(asr32(int32_t(D(3)), 8));
  D(6) = uint32_t(int32_t(int16_t(lo(6))) * sw(A(4) + 8));
  D(6) = uint32_t(asr32(int32_t(D(6)), 8));
  WW(-120, lo(3));
  WW(-118, lo(6));
  WW(-116, uint16_t(A(5)));
  do {                                                // $32e72
    setw(2, rd16(A(1)));
    A(1) += 2;
    setw(4, rd16(A(1)));
    A(1) += 2;
    setw(7, RW(-8));
    setw(2, asr16(lo(2), D(7)));
    setw(4, asr16(lo(4), D(7)));
    setw(2, lo(2) + lo(0));
    setw(4, lo(4) + lo(1));
    A(2) = A(4);
    row_xz(2, 3);
    row_xz(2, 6);
    last_xz(2, uint16_t(A(5)));
    wr16(A(0), rd16(A(0)) - 1);
  } while (sw(A(0)) > 0);
  WW(-124, 0);
  A(2) = rd32(0x5ad2e);
  WW(-106, 0);
L32ec8:
  if (RW(-106) != 0) goto L32efe;
  setw(0, rd16(A(2)));
  A(2) += 2;
  if (int16_t(lo(0)) >= 0) goto L32f20;
  if (lo(0) == 0xFFFF) goto L32efe;
  setw(1, lo(0));
  setw(1, lo(1) & 0x4000);
  if (lo(1) != 0) WW(-106, RW(-106) + 1);
  setw(0, lo(0) & 16383);
  A(0) = 0x33478;
  A(0) = rd32(A(0) + uint32_t(e16(lo(0))));
  call_c(A(0));
  flags(lo(0), 16);
  if (int16_t(lo(0)) < 0) goto L32f06;
  WW(-124, RW(-124) | lo(0));
  goto L32ec8;
L32efe:
  setw(0, RW(-124));
  leave_frame();
  flags(lo(0), 16);
  return;
L32f06:
  D(0) = 0;
  leave_frame();
  flags(0, 32);
  return;
L32f0c:
  setw(0, rd16(A(2)));
  A(2) += 2;
  if (int16_t(lo(0)) < 0) goto L32efe;
  setw(1, rd16(0x5adb0));
  setw(0, asr16(lo(0), D(1)));
  if (int16_t(lo(0)) < int16_t(RW(-40))) goto L32efe;
  A(2) = rd32(A(2));
L32f20:
  if (lo(0) != 32767) {
    setw(1, rd16(0x5adb0));
    setw(0, asr16(lo(0), D(1)));
    if (int16_t(lo(0)) < int16_t(RW(-40))) goto L32f0c;
    int16_t t = sw(A(2));
    A(2) += 2;
    if (t >= 0) A(2) += 2;
  }
  setw(0, rd16(A(2)));                                // $32f3a
  A(2) += 2;
  if (lo(0) != 0) {
    setw(7, rd16(A(2)));
    A(2) += 2;
    s32f6e();
  }
  setw(0, rd16(A(2)));
  A(2) += 2;
  setw(1, lo(0));
  setw(1, lo(1) & 0x4000);
  if (lo(1) != 0) WW(-106, RW(-106) + 1);
  setw(0, lo(0) & 16383);
  A(0) = 0x33478;
  A(0) = rd32(A(0) + uint32_t(e16(lo(0))));
  call_c(A(0));
  flags(lo(0), 16);
  if (int16_t(lo(0)) < 0) goto L32f06;
  WW(-124, RW(-124) | lo(0));
  goto L32ec8;
}
void entry_32d7e() {
  cpu().push32(cpu().a[6]);
  cpu().a[6] = cpu().a[7];
  cpu().a[7] -= 152;
  distant_body();
}
void entry_32d64() {
  if (sl(0x5ad5e) < -3670016) {
    D(0) = 0;
    flags(0, 32);
    return;
  }
  entry_32d7e();
}
void entry_32d72() {
  if (sl(0x5ad5e) < -8388608) {
    D(0) = 0;
    flags(0, 32);
    return;
  }
  entry_32d7e();
}
NATIVE_ASM(0x32d64, entry_32d64, true);
NATIVE_ASM(0x32d72, entry_32d72, true);

}  // namespace
}  // namespace native
