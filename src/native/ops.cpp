// Polygon-op handlers of the 3D renderer: the routines behind the jump
// table at $33478 ($335b0-$35f44).  They run inside the `link a6,#-152`
// frame of the object draw routines (render.cpp): a2 is the op stream, a6
// the frame, and results come back in d0 (0 = continue, 1/-1 = stop).
// Register-convention assembly on the live register file.
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

void negw(int r) { setw(r, uint16_t(-int16_t(lo(r)))); }
void negl(int r) { D(r) = uint32_t(-int32_t(D(r))); }
void movemw(uint32_t a, int r0, int n) {              // movem.w (a),d<r0>..
  for (int i = 0; i < n; ++i) D(r0 + i) = uint32_t(e16(rd16(a + 2u * uint32_t(i))));
}
void ret(int32_t v) {
  D(0) = uint32_t(v);
  flags(uint32_t(v), 32);
}
uint16_t next16() {                                   // (a2)+
  uint16_t v = rd16(A(2));
  A(2) += 2;
  return v;
}
// movem.l a1/a5,-(sp) etc.: push in the order the 68000 stores them
void push_a1_a5() { cpu().push32(A(5)); cpu().push32(A(1)); }
void pop_a1_a5() { A(1) = cpu().pop32(); A(5) = cpu().pop32(); }
void push_a1_a2() { cpu().push32(A(2)); cpu().push32(A(1)); }
void pop_a1_a2() { A(1) = cpu().pop32(); A(2) = cpu().pop32(); }
void push_a1_a2_a5() { cpu().push32(A(5)); cpu().push32(A(2)); cpu().push32(A(1)); }
void pop_a1_a2_a5() { A(1) = cpu().pop32(); A(2) = cpu().pop32(); A(5) = cpu().pop32(); }
// $32ad2 (distance test) returns Z in the flags
bool call_32ad2() {
  call_c(0x32ad2);
  return cpu().fz;
}

// ---- $335b0..$33792: simple ops --------------------------------------------
void op_335b0() { ret(0); }
void op_335b4() {                                     // $438a0 with a vertex triple
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  movemw(A(3), 0, 3);
  wr16(0x5ac4c, next16());
  push_a1_a2();
  call_c(0x438a0);
  pop_a1_a2();
}
void op_335d6() {                                     // same with $438b0
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  movemw(A(3), 0, 3);
  wr16(0x5ac4c, next16());
  push_a1_a2();
  call_c(0x438b0);
  pop_a1_a2();
}
void op_335f8() {
  A(4) = 0x5d47c;
  setw(0, rd16(0x5acac));
  setw(0, asl16(lo(0), 5));
  A(4) += uint32_t(e16(lo(0)));
  setw(0, rd16(A(4) + 4));
  if (int16_t(lo(0)) >= 0) {
    setw(2, lo(0));
    D(1) = 2;
    setw(0, lo(0) & 7);
    setw(0, lo(0) - 1);
    if (int16_t(lo(0)) > 0) {
      setw(1, lo(1) + 134);
      setw(0, lo(0) - 2);
      if (int16_t(lo(0)) > 0) {
        setw(1, lo(1) + 134);
        setw(0, lo(0) - 2);
        if (int16_t(lo(0)) > 0) setw(1, lo(1) + 134);
      }
    }
    A(2) += uint32_t(e16(lo(1)));
    if (int16_t(lo(2)) < 12) {
      setw(2, lo(2) - 11);
      negw(2);
      setw(2, asr16(lo(2), 1));
      setw(2, asl16(lo(2), 3));
      A(2) += 38;
      A(2) += uint32_t(e16(lo(2)));
    } else if (int16_t(lo(2)) > 116) {
      setw(2, lo(2) - 128);
      setw(2, lo(2) + 12);
      setw(2, asr16(lo(2), 1));
      setw(2, asl16(lo(2), 3));
      A(2) += 86;
      A(2) += uint32_t(e16(lo(2)));
    }
  }
  ret(0);
}
void op_33664() {
  if (rd16(0x5acac) == 87) A(2) += 2;
  ret(0);
}
void skip_33690() {                                   // $33692 dbf loop
  for (;;) {
    setw(1, lo(1) - 1);
    if (lo(1) == 0xFFFF) break;
    A(2) += uint32_t(e16(lo(0)));
  }
  ret(0);
}
void op_33674() {
  D(0) = 18;
  setw(1, rd16(0x5abd2) & 15);
  skip_33690();
}
void op_33682() {
  D(0) = 52;
  setw(1, rd16(0x5abd2) & 15);
  skip_33690();
}
// three vertices (index words d0-d2) copied to $61288, then the test $32ad2
void copy3_to_61288() {
  A(0) = 0x61288;
  A(3) = 0x5d684;
  movemw(A(2), 0, 3);
  A(2) += 6;
  for (int i = 0; i < 3; ++i) {
    uint32_t v = A(3) + uint32_t(e16(lo(i)));
    wr32(A(0), rd32(v));
    wr16(A(0) + 4, rd16(v + 4));
    A(0) += 6;
  }
  A(3) = uint32_t(e16(next16()));
  setw(7, uint16_t(A(3)));
}
void op_3369a() {
  copy3_to_61288();
  if (call_32ad2()) return;                           // Z: rts, d0 from the callee
  A(2) += 18;
}
void op_336d6() {
  copy3_to_61288();
  if (!call_32ad2()) {
    ret(0);
    return;
  }
  movemw(0x61288, 0, 3);
  wr16(0x5ac4c, uint16_t(A(3)));
  push_a1_a2();
  call_c(0x438a0);
  pop_a1_a2();
}
void line_op(uint32_t routine) {                      // $3374a
  A(4) = routine;
  wr32(0x5a9de, 0xFFFFFFFFu);
  A(0) = 0x61886;
  A(3) = 0x5d684;
  setw(1, next16());
  wr32(A(0), rd32(A(3) + uint32_t(e16(lo(1)))));
  A(0) += 4;
  setw(6, rd16(A(3) + 4 + uint32_t(e16(lo(1)))));
  wr16(A(0), lo(6));
  A(0) += 2;
  setw(1, next16());
  wr32(A(0), rd32(A(3) + uint32_t(e16(lo(1)))));
  A(0) += 4;
  setw(7, rd16(A(3) + 4 + uint32_t(e16(lo(1)))));
  wr16(0x5ac4c, next16());
  setw(6, lo(6) & lo(7));
  if (int16_t(lo(6)) < 0) {
    ret(0);
    return;
  }
  wr16(A(0), lo(7));
  A(0) += 2;
  push_a1_a2_a5();
  call_c(A(4));
  pop_a1_a2_a5();
}
void op_3372c() { line_op(0x4397c); }
void op_33734() {
  if (sl(0x5ad70) < -192) {
    line_op(0x43a56);
    return;
  }
  A(2) += 6;
  ret(0);
}
void op_33744() { line_op(0x43a56); }

// ---- $3383e: shared tail of the polygon ops ------------------------------------
// a2 -> next op word (sign-extended into a3); runs the $32ad2 test and draws.
void tail_3383e() {
  A(3) = uint32_t(e16(next16()));
  setw(7, uint16_t(A(3)));
  if (int16_t(lo(7)) >= 0) {
    WW(-50, RW(-50) + 1);
    if (!call_32ad2()) {
      setw(1, uint16_t(A(3)));
      setw(1, lo(1) & 0x4000);
      if (lo(1) == 0) {
        ret(-1);
        return;
      }
      setw(7, next16());
    } else {
      WW(-52, RW(-52) + 1);
      setw(1, uint16_t(A(3)));
      setw(7, lo(1));
      setw(1, lo(1) & 0x4000);
      if (lo(1) != 0) A(2) += 2;
    }
  }
  wr16(0x5ac4c, lo(7));                               // $3386c
  push_a1_a2_a5();
  call_c(0x38c0a);
  pop_a1_a2_a5();
}

void op_33792() {
  A(3) = 0x5d684;
  A(0) = 0x61284;
  wr32(A(0), 4);
  A(0) += 4;
  movemw(A(2), 1, 3);
  A(2) += 6;
  for (int i = 1; i <= 3; ++i) {
    uint32_t v = A(3) + uint32_t(e16(lo(i)));
    wr32(A(0), rd32(v));
    wr16(A(0) + 4, rd16(v + 4));
    A(0) += 6;
  }
  movemw(A(0) - 18, 2, 6);
  setw(0, lo(4));
  setw(0, lo(0) & lo(7));
  setw(5, lo(5) - lo(2));
  setw(6, lo(6) - lo(3));
  setw(7, lo(7) - lo(4));
  movemw(A(0) - 6, 2, 3);
  setw(0, lo(0) & lo(4));
  setw(2, lo(2) - lo(5));
  setw(3, lo(3) - lo(6));
  setw(4, lo(4) - lo(7));
  setw(0, lo(0) & lo(4));
  if (int16_t(lo(0)) < 0) {
    ret(-1);
    return;
  }
  wr16(A(0), lo(2));
  wr16(A(0) + 2, lo(3));
  wr16(A(0) + 4, lo(4));
  tail_3383e();
}

void op_337ec() {
  A(0) = 0x61284;
  wr16(A(0), 0);
  A(0) += 2;
  A(0) += 2;
  A(3) = 0x5d684;
  D(7) = 3;
  auto vtx = [&](bool first) {                        // lea (a3,d1),a4; long+word
    A(4) = A(3) + uint32_t(e16(lo(1)));
    wr32(A(0), rd32(A(4)));
    A(0) += 4;
    A(4) += 4;
    if (first) {
      setw(6, rd16(A(4)));
      wr16(A(0), lo(6));
    } else {
      wr16(A(0), rd16(A(4)));
      setw(6, lo(6) & rd16(A(4)));
    }
    A(0) += 2;
  };
  setw(1, next16());
  vtx(true);
  setw(1, next16());
  vtx(false);
  for (;;) {                                          // $33816
    setw(1, next16());
    if (int16_t(lo(1)) < 0) break;
    vtx(false);
    setw(7, lo(7) + 1);
  }
  setw(1, lo(1) & 0x7FFF);                            // $33828
  vtx(false);
  if (int16_t(lo(6)) < 0) {
    ret(-1);
    return;
  }
  wr16(0x61286, lo(7));
  tail_3383e();
}

// $33890: list of sub-polygons (a4 -> table of word offsets)
void op_33890() {
  push_a1_a5();
  WW(-126, 0);
  wr16(0x61284, 0);
  A(4) = rd32(A(2));
  A(2) += 4;
  for (;;) {                                          // $338a0
    setw(1, next16());
    if (int16_t(lo(1)) < 0) break;
    cpu().push32(A(4));
    A(4) += uint32_t(e16(lo(1)));
    A(0) = 0x61288;
    A(3) = 0x5d684;
    D(7) = 3;
    auto rd4 = [&] {                                  // (a4)+ -> vertex copy
      setw(1, rd16(A(4)));
      A(4) += 2;
    };
    auto put = [&](bool first) {
      uint32_t v = A(3) + uint32_t(e16(lo(1)));
      wr32(A(0), rd32(v));
      A(0) += 4;
      if (first) {
        setw(6, rd16(v + 4));
        wr16(A(0), lo(6));
      } else {
        wr16(A(0), rd16(v + 4));
        setw(6, lo(6) & rd16(A(0)));
      }
      A(0) += 2;
    };
    rd4();
    put(true);
    rd4();
    put(false);
    for (;;) {                                        // $338d0
      rd4();
      if (int16_t(lo(1)) < 0) break;
      put(false);
      setw(7, lo(7) + 1);
    }
    setw(1, lo(1) & 0x7FFF);                          // $338e4
    put(false);
    if (int16_t(lo(6)) < 0) {
      A(4) = cpu().pop32();
      continue;
    }
    wr16(0x61286, lo(7));
    A(3) = uint32_t(e16(rd16(A(4))));
    A(4) += 2;
    setw(7, uint16_t(A(3)));
    if (int16_t(lo(7)) >= 0) {
      if (!call_32ad2()) {
        setw(1, uint16_t(A(3)));
        setw(1, lo(1) & 0x4000);
        if (lo(1) == 0) {
          A(4) = cpu().pop32();
          continue;
        }
        setw(7, rd16(A(4)));
        A(4) += 2;
      } else {
        setw(7, uint16_t(A(3)));
      }
    }
    wr16(0x5ac4c, lo(7));                             // $33918
    cpu().push32(A(2));
    call_c(0x38c0a);
    WW(-126, RW(-126) | lo(0));
    A(2) = cpu().pop32();
    A(4) = cpu().pop32();
  }
  pop_a1_a5();                                        // $33886
  setw(0, RW(-126));
}

// ---- $33936: clipped polygon list with a depth/zoom prologue -------------------
void op_33936() {
  D(0) = rd32(0x5ad5e);
  A(4) = 0x5b47c + uint32_t(e16(rd16(0x5acae)));
  setb(3, rd8(A(4) + 4));
  setb(7, lob(3));
  setb(7, lob(7) & 0xC0);
  if (lob(7) != 0 && lob(7) != 0xC0) return ret(1);
  setb(3, lob(3) & 0x40);                             // $3395a
  if (lob(3) != 0) {
    setw(7, rd16(A(4) + 78));
    negw(7);
    extl(7);
    D(7) = uint32_t(asl32(int32_t(D(7)), 8));
    if (int32_t(D(0)) >= int32_t(D(7))) return ret(1);
    D(0) -= D(7);
    setw(3, rd16(0x5adb0));
    D(0) = uint32_t(asr32(int32_t(D(0)), D(3)));
    WL(-144, D(0));
  } else {
    WL(-144, RL(-28));
  }
  if (int32_t(D(0)) < -1048576) return ret(-1);       // $33984
  if (rd8(0x5aa7d) == 0 && int8_t(rd8(0x5ab63)) < 3) {
    setw(7, rd16(0x5acac));
    if (lo(7) == rd16(0x5abd4)) {
      if (rd8(0x5ab63) == 0) return ret(1);
      D(2) = 0xFFFFE000u;
    } else if (rd8(A(4) + 98) != 48 && int32_t(D(0)) < -65536 && rd8(A(4) + 98) != 20) {
      if (int8_t(rd8(0x5ab63)) <= 1) return ret(1);
      D(2) = 0xFFF60000u;
    } else {
      goto L339ec;
    }
    if (int32_t(D(0)) < int32_t(D(2))) return ret(1);
  }
L339ec:
  push_a1_a5();
  setw(0, next16());
  extl(0);
  D(0) = uint32_t(asl32(int32_t(D(0)), 8));
  if (int32_t(D(0)) < sl(A(4) + 24)) goto L33a82;
  A(4) += 164;
  WW(-2, rd16(0x5adb0));
  D(2) = D(0);
  negl(0);
  swapw(0);
  extl(0);
  D(0) = uint32_t(asr32(int32_t(D(0)), 5));
  {
    int32_t t = int16_t(lo(0)) - int16_t(RW(-2));
    setw(0, uint32_t(t));
    if (t <= 0) {
      setw(0, 0);
      WL(-148, RL(-32));
      WL(-140, RL(-24));
    } else {                                          // $33a2a
      WW(-2, RW(-2) + lo(0));
      D(1) = RL(-32);
      D(3) = RL(-24);
      D(1) = uint32_t(asr32(int32_t(D(1)), D(0)));
      D(3) = uint32_t(asr32(int32_t(D(3)), D(0)));
      setw(0, RW(-2));
      D(2) = uint32_t(asr32(int32_t(D(2)), D(0)));
      WL(-148, D(1));
      WL(-144, D(2));
      WL(-140, D(3));
    }
  }
  D(1) = RL(-148);                                    // $33a46
  D(2) = RL(-144);
  D(3) = RL(-140);
  D(4) = rd32(0x5ae28);
  D(5) = rd32(0x5ae30);
  D(4) = uint32_t(asr32(int32_t(D(4)), D(0)));
  D(5) = uint32_t(asr32(int32_t(D(5)), D(0)));
  D(1) += D(4);
  D(3) += D(5);
  D(7) = 8;
  setw(7, lo(7) - RW(-6));
  D(1) = uint32_t(asr32(int32_t(D(1)), D(7)));
  D(2) = uint32_t(asr32(int32_t(D(2)), D(7)));
  D(3) = uint32_t(asr32(int32_t(D(3)), D(7)));
  WW(-20, lo(1));
  WW(-18, lo(2));
  WW(-16, lo(3));
  wr16(0x5ac4c, 0);
  wr16(0x61284, 0);
  goto L33a9a;
L33a8a:
  setw(0, next16());
  if (int16_t(lo(0)) < 0) goto L33a82;
  extl(0);
  A(5) = A(4) - 148;
  if (int32_t(D(0)) < sl(A(5))) goto L33a82;
L33a9a:
  if (rd16(A(2) + 2) == 0) {
    movemw(A(2) + 4, 1, 3);
    setw(0, rd16(A(4) + uint32_t(e16(lo(1)))));
    setw(5, rd16(A(4) + uint32_t(e16(lo(2)))));
    setw(5, lo(5) - lo(0));
    setw(6, rd16(A(4) + uint32_t(e16(lo(3)))));
    setw(6, lo(6) - lo(0));
    setw(4, rd16(A(4) + 4 + uint32_t(e16(lo(1)))));
    setw(1, rd16(A(4) + 4 + uint32_t(e16(lo(2)))));
    setw(1, lo(1) - lo(4));
    setw(3, rd16(A(4) + 4 + uint32_t(e16(lo(3)))));
    setw(3, lo(3) - lo(4));
    D(6) = uint32_t(int32_t(int16_t(lo(6))) * int16_t(lo(1)));
    D(3) = uint32_t(int32_t(int16_t(lo(3))) * int16_t(lo(5)));
    D(6) -= D(3);
    D(6) = uint32_t(asr32(int32_t(D(6)), 8));
    if (int32_t(D(6)) < 0) {
      setw(7, next16());
      setw(7, lo(7) + lo(7));
      setw(7, lo(7) + 2);
      A(2) += uint32_t(e16(lo(7)));
      goto L33a8a;
    }
  }
  setw(7, next16());                                  // $33ada
  A(2) += 2;
  A(0) = 0x61286;
  wr16(A(0), lo(7));
  A(0) += 2;
  WW(-14, lo(7));
  do {
    setw(7, next16());                                // $33aea
    setw(5, RW(-8));
    setw(2, rd16(A(4) + uint32_t(e16(lo(7)))));
    setw(2, asr16(lo(2), lo(5)));
    setw(4, rd16(A(4) + 4 + uint32_t(e16(lo(7)))));
    setw(4, asr16(lo(4), lo(5)));
    setw(2, lo(2) + RW(-20));
    setw(3, RW(-18));
    setw(4, lo(4) + RW(-16));
    A(5) = 0x5aed0;
    for (int row = 0; row < 2; ++row) {
      setw(5, lo(2));
      setw(6, lo(3));
      setw(7, lo(4));
      D(5) = uint32_t(int32_t(int16_t(lo(5))) * sw(A(5)));
      A(5) += 2;
      D(6) = uint32_t(int32_t(int16_t(lo(6))) * sw(A(5)));
      A(5) += 2;
      D(7) = uint32_t(int32_t(int16_t(lo(7))) * sw(A(5)));
      A(5) += 2;
      D(7) += D(6);
      D(7) += D(5);
      D(7) = uint32_t(asr32(int32_t(D(7)), 8));
      wr16(A(0), lo(7));
      A(0) += 2;
    }
    D(2) = uint32_t(int32_t(int16_t(lo(2))) * sw(A(5)));
    A(5) += 2;
    D(3) = uint32_t(int32_t(int16_t(lo(3))) * sw(A(5)));
    A(5) += 2;
    D(4) = uint32_t(int32_t(int16_t(lo(4))) * sw(A(5)));
    A(5) += 2;
    D(4) += D(3);
    D(4) += D(2);
    D(4) = uint32_t(asr32(int32_t(D(4)), 8));
    wr16(A(0), lo(4));
    A(0) += 2;
    WW(-14, RW(-14) - 1);
  } while (int16_t(RW(-14)) > 0);
  cpu().push32(A(4));                                 // movem.l a2/a4,-(sp)
  cpu().push32(A(2));
  call_c(0x38c0a);
  A(2) = cpu().pop32();
  A(4) = cpu().pop32();
  goto L33a8a;
L33a82:
  pop_a1_a5();
  ret(1);
}

// ---- $33b60: ---------------------------------------------------------------
void op_33b5c() { ret(-1); }

void divsw(uint32_t& dn, uint16_t divisor) {          // signed 32 / 16
  if (divisor == 0) {
    mach().log("native ops: division by zero at %06x\n", cpu().op_pc);
    abort();
  }
  int32_t n = int32_t(dn), d = int16_t(divisor);
  int64_t q = int64_t(n) / d;
  if (q > 32767 || q < -32768) return;                // overflow: unchanged
  int32_t r = int32_t(int64_t(n) % d);
  dn = (uint32_t(uint16_t(r)) << 16) | uint16_t(q);
}
void mstore(uint32_t a, std::initializer_list<int> regs) {   // movem.w regs,(a)
  uint32_t i = 0;
  for (int r : regs) {
    wr16(a + 2 * i, r < 8 ? lo(r) : uint16_t(A(r - 8)));
    ++i;
  }
}
// movem.w (a3)+,d0-d7/a4 : 9 words, sign-extended
void load9(uint32_t a) {
  movemw(a, 0, 8);
  A(4) = uint32_t(e16(rd16(a + 16)));
}
// the side tests shared by $33b60 and $33d22: stop (-1) when the box a3+6..
// lies outside, else d6/d7 = distance deltas and d0 selects the half
bool box_reject() {
  movemw(A(3) + 6, 1, 3);
  if (int16_t(lo(1)) > int16_t(lo(3))) return true;
  negw(1);
  if (int16_t(lo(1)) > int16_t(lo(3))) return true;
  if (int16_t(lo(2)) > int16_t(lo(3))) return true;
  negw(2);
  if (int16_t(lo(2)) > int16_t(lo(3))) return true;
  return false;
}
// returns true when the "swap d0" path needs the second ($33bec) form
void view_delta() {
  setw(6, rd16(0x5ad6a));
  setw(7, rd16(0x5ad6e));
  negw(6);
  negw(7);
  setw(2, rd16(0x5ae22));
  setw(3, rd16(0x5ae26));
  setw(5, rd16(0x5adb0));
  setw(2, asl16(lo(2), lo(5)));
  setw(3, asl16(lo(3), lo(5)));
}

void op_33b60() {
  WW(-126, 0);
  D(0) = rd32(A(2));
  A(2) += 4;
  WW(-94, next16());
  A(3) = 0x5d684;
  if (box_reject()) return ret(-1);
  push_a1_a2_a5();
  A(0) = 0x61284;
  wr32(A(0), 4);
  A(0) += 4;
  view_delta();
  {
    int t = int16_t(lo(6)) - int16_t(lo(2));
    setw(6, uint32_t(t));
    bool first;                                       // true: $33bd0 form -> $33c8e
    if (t < 0) {                                      // $33bda
      int u = int16_t(lo(7)) - int16_t(lo(3));
      setw(7, uint32_t(u));
      if (u >= 0) {
        negw(6);
        first = true;
      } else {
        negw(6);
        negw(7);
        first = false;
      }
      if (first) {
        if (!(int16_t(lo(7)) > int16_t(lo(6)))) swapw(0);
        wr16(0x5ac4c, lo(0));
        goto L33c8e;
      }
    } else {
      int u = int16_t(lo(7)) - int16_t(lo(3));
      setw(7, uint32_t(u));
      if (u < 0) {
        negw(7);
        if (!(int16_t(lo(7)) > int16_t(lo(6)))) swapw(0);
        wr16(0x5ac4c, lo(0));
        goto L33c8e;
      }
    }
  }
  if (!(int16_t(lo(7)) > int16_t(lo(6)))) swapw(0);   // $33be6
  wr16(0x5ac4c, lo(0));
  load9(A(3));                                        // $33bf2
  A(3) += 18;
  mstore(A(0), {0, 1, 2, 3, 4, 5});
  setw(0, lo(0) - lo(3));
  setw(1, lo(1) - lo(4));
  setw(2, lo(2) - lo(5));
  setw(6, lo(6) - lo(3));
  setw(7, lo(7) - lo(4));
  A(4) -= uint32_t(e16(lo(5)));
  movemw(A(3), 3, 3);
  setw(3, lo(3) + lo(6));
  setw(4, lo(4) + lo(7));
  setw(5, lo(5) + uint16_t(A(4)));
  mstore(A(0) + 12, {3, 4, 5});
  setw(3, lo(3) + lo(0));
  setw(4, lo(4) + lo(1));
  setw(5, lo(5) + lo(2));
  mstore(A(0) + 18, {3, 4, 5});
  call_c(0x38c0a);
  WW(-126, RW(-126) | lo(0));
  wr16(0x5ac4c, RW(-94));
  if (rd8(0x5aa7e) == 0) goto L33d14;
  A(0) = 0x61288;
  A(3) = 0x5d684;
  wr32(A(0) + 12, rd32(A(0) + 18));
  wr16(A(0) + 16, rd16(A(0) + 22));
  load9(A(3));
  setw(6, lo(6) - lo(3));
  setw(7, lo(7) - lo(4));
  A(4) -= uint32_t(e16(lo(5)));
  setw(3, lo(0));
  setw(4, lo(1));
  setw(5, lo(2));
  setw(0, lo(0) + lo(6));
  setw(1, lo(1) + lo(7));
  setw(2, lo(2) + uint16_t(A(4)));
  mstore(A(0) + 6, {0, 1, 2});
  setw(0, lo(0) - rd16(A(0) + 12));
  setw(1, lo(1) - rd16(A(0) + 14));
  setw(2, lo(2) - rd16(A(0) + 16));
  setw(3, lo(3) - lo(0));
  setw(4, lo(4) - lo(1));
  setw(5, lo(5) - lo(2));
  mstore(A(0) + 18, {3, 4, 5});
  goto L33d0a;
L33c8e:
  load9(A(3));
  A(3) += 18;
  mstore(A(0), {6, 7, 12});
  setw(0, lo(0) - lo(3));
  setw(1, lo(1) - lo(4));
  setw(2, lo(2) - lo(5));
  movemw(A(3), 3, 3);
  mstore(A(0) + 6, {3, 4, 5});
  setw(3, lo(3) + lo(0));
  setw(4, lo(4) + lo(1));
  setw(5, lo(5) + lo(2));
  setw(6, lo(6) + lo(0));
  setw(7, lo(7) + lo(1));
  A(4) += uint32_t(e16(lo(2)));
  mstore(A(0) + 12, {3, 4, 5, 6, 7, 12});
  call_c(0x38c0a);
  WW(-126, RW(-126) | lo(0));
  wr16(0x5ac4c, RW(-94));
  if (rd8(0x5aa7e) == 0) goto L33d14;
  A(0) = 0x61288;
  A(3) = 0x5d684;
  movemw(A(3), 0, 3);
  mstore(A(0), {0, 1, 2});
  movemw(A(0) + 12, 3, 5);
  A(4) = uint32_t(e16(rd16(A(0) + 22)));
  mstore(A(0) + 6, {6, 7, 12});
  mstore(A(0) + 18, {3, 4, 5});
  setw(3, lo(3) - lo(0));
  setw(4, lo(4) - lo(1));
  setw(5, lo(5) - lo(2));
  setw(6, lo(6) + lo(3));
  setw(7, lo(7) + lo(4));
  A(4) += uint32_t(e16(lo(5)));
  mstore(A(0) + 12, {6, 7, 12});
L33d0a:
  call_c(0x38c0a);
  WW(-126, RW(-126) | lo(0));
L33d14:
  pop_a1_a2_a5();
  setw(0, RW(-126));
}

void op_33d22() {
  D(0) = rd32(A(2));
  A(2) += 4;
  A(3) = 0x5d684;
  if (box_reject()) return ret(-1);
  A(0) = 0x61284;
  wr32(A(0), 3);
  A(0) += 4;
  view_delta();
  {
    int t = int16_t(lo(6)) - int16_t(lo(2));
    setw(6, uint32_t(t));
    bool second = false;                              // $33d96 form
    if (t < 0) {
      int u = int16_t(lo(7)) - int16_t(lo(3));
      setw(7, uint32_t(u));
      negw(6);
      if (u < 0) {
        negw(7);
        second = true;
      }
    } else {
      int u = int16_t(lo(7)) - int16_t(lo(3));
      setw(7, uint32_t(u));
      if (u >= 0) {
        second = true;
      } else {
        negw(7);
      }
    }
    if (!(int16_t(lo(7)) > int16_t(lo(6)))) swapw(0);
    setw(7, lo(0));
    if (second) {                                     // $33d9e
      wr32(A(0), rd32(A(3)));
      wr16(A(0) + 4, rd16(A(3) + 4));
      wr32(A(0) + 6, rd32(A(3) + 6));
      wr16(A(0) + 10, rd16(A(3) + 10));
      wr32(A(0) + 12, rd32(A(3) + 24));
      wr16(A(0) + 16, rd16(A(3) + 28));
    } else {                                          // $33db8
      wr32(A(0), rd32(A(3)));
      wr16(A(0) + 4, rd16(A(3) + 4));
      wr32(A(0) + 6, rd32(A(3) + 12));
      wr16(A(0) + 10, rd16(A(3) + 16));
      wr32(A(0) + 12, rd32(A(3) + 18));
      wr16(A(0) + 16, rd16(A(3) + 22));
    }
    A(0) += 18;
  }
  wr16(0x5ac4c, lo(7));                               // $33dd0
  push_a1_a2_a5();
  call_c(0x38c0a);
  pop_a1_a2_a5();
}

void vertex_to(uint32_t& a0, bool advance_last) {     // lea (a3,d1),a4; long+word
  A(4) = A(3) + uint32_t(e16(lo(1)));
  wr32(a0, rd32(A(4)));
  a0 += 4;
  A(4) += 4;
  wr16(a0, rd16(A(4)));
  if (advance_last) a0 += 2;
}
void op_33de6() {
  push_a1_a5();
  A(0) = 0x61288;
  A(3) = 0x5d684;
  for (int i = 0; i < 3; ++i) {
    setw(1, next16());
    vertex_to(A(0), i < 2);
  }
  WW(-50, RW(-50) + 1);
  A(3) = rd32(A(2));                                  // moveal (a2)+,a3 (long)
  A(2) += 4;
  setw(7, uint16_t(A(3)));
  if (call_32ad2()) WW(-52, RW(-52) + 1);
  wr16(0x5ac4c, next16());
  WW(-10, next16());
  A(3) = 0x5d684;
  for (;;) {                                          // $33e38
    WW(-10, RW(-10) - 1);
    if (int16_t(RW(-10)) < 0) break;
    D(6) = 0xFFFFFFFFu;
    A(0) = 0x61886;
    setw(1, next16());
    vertex_to(A(0), true);
    setw(6, lo(6) & rd16(A(4)));
    setw(1, next16());
    vertex_to(A(0), false);
    setw(6, lo(6) & rd16(A(4)));
    if (lo(6) != 0) continue;
    cpu().push32(A(2));
    call_c(0x43a56);
    A(2) = cpu().pop32();
  }
  pop_a1_a5();
  ret(1);
}

// $33e74: interpolated polylines (n points between two vertex pairs)
void op_33e74() {
  WW(-126, 0);
  A(3) = 0x5d684;
  A(4) = 0x60084;
  A(0) = A(4) + 12;
  setw(0, next16());
  WW(-48, lo(0));
  auto load_pairs = [&](int ireg) {                   // d2-d4 / d5-d7 <<4, delta
    setw(ireg, next16());
    movemw(A(3) + uint32_t(e16(lo(ireg))), 2, 3);
    setw(ireg, next16());
    movemw(A(3) + uint32_t(e16(lo(ireg))), 5, 3);
    for (int r = 2; r <= 7; ++r) {
      setw(r, lo(r));
      extl(r);
      D(r) = uint32_t(asl32(int32_t(D(r)), 4));
    }
    D(5) -= D(2);
    D(6) -= D(3);
    D(7) -= D(4);
  };
  load_pairs(1);
  divsw(D(5), lo(0));
  divsw(D(6), lo(0));
  divsw(D(7), lo(0));
  extl(5);
  extl(6);
  extl(7);
  setw(1, lo(0));
  setw(0, lo(0) - 2);
  do {
    D(2) += D(5);
    D(3) += D(6);
    D(4) += D(7);
    wr32(A(4), D(2));
    wr32(A(4) + 4, D(3));
    wr32(A(4) + 8, D(4));
    A(4) += 24;
    setw(0, lo(0) - 1);
  } while (lo(0) != 0xFFFF);
  load_pairs(0);
  divsw(D(5), lo(1));
  divsw(D(6), lo(1));
  divsw(D(7), lo(1));
  extl(5);
  extl(6);
  extl(7);
  setw(1, lo(1) - 2);
  do {
    D(2) += D(5);
    D(3) += D(6);
    D(4) += D(7);
    wr32(A(0), D(2));
    wr32(A(0) + 4, D(3));
    wr32(A(0) + 8, D(4));
    A(0) += 24;
    setw(1, lo(1) - 1);
  } while (lo(1) != 0xFFFF);
  wr16(0x5ac4c, next16());
  push_a1_a2_a5();
  A(4) = 0x60084;
  WW(-48, RW(-48) - 1);
  do {                                                // $33f40
    for (int r = 1; r <= 6; ++r) {
      D(r) = rd32(A(4));
      A(4) += 4;
    }
    for (int r = 1; r <= 6; ++r) {
      bool c = (D(r) >> 3) & 1;
      D(r) = uint32_t(asr32(int32_t(D(r)), 4));
      if (c) setw(r, lo(r) + 1);
    }
    mstore(0x61886, {1, 2, 3, 4, 5, 6});
    cpu().push32(A(4));
    call_c(0x43a56);
    WW(-126, RW(-126) | lo(0));
    A(4) = cpu().pop32();
    WW(-48, RW(-48) - 1);
  } while (int16_t(RW(-48)) > 0);
  pop_a1_a2_a5();
  setw(0, RW(-126));
}

void op_33f8e() {
  if (rd8(0x5aa7d) == 0) {
    A(3) = 0x5b47c + uint32_t(e16(rd16(0x5abd6)));
    setb(1, rd8(A(3) + 4));
    setb(1, lob(1) & 0x40);
    if (lob(1) != 0) WW(-122, 1);
  }
  ret(0);
}
void op_33fb6() { ret(0); }
void op_33fba() {
  A(3) = rd32(0x5ad2a);
  A(3) += 10;
  A(3) += uint32_t(e16(next16()));
  if (sl(0x5ad70) <= -320) return ret(0);
  if (rd8(0x5ab64) != 0) return ret(0);
  setw(5, rd16(A(3)));
  A(3) += 2;
  setw(7, rd16(A(3)));
  A(3) += 2;
  setw(5, lo(5) - rd16(A(3)));
  A(3) += 2;
  setw(7, lo(7) - rd16(A(3)));
  A(3) += 2;
  negw(5);
  negw(7);
  setw(6, 0);
  setw(0, 256);
  call_c(0x39cb6);
  cpu().a[7] -= 6;
  wr16(cpu().a[7], lo(5));
  wr16(cpu().a[7] + 2, lo(6));
  wr16(cpu().a[7] + 4, lo(7));
  setw(5, RW(-38));
  setw(5, lo(5) - rd16(0x5ae22));
  setw(6, 0);
  setw(7, RW(-34));
  setw(7, lo(7) - rd16(0x5ae26));
  setw(0, 256);
  call_c(0x39cb6);
  movemw(cpu().a[7], 0, 3);
  cpu().a[7] += 6;
  D(0) = uint32_t(int32_t(int16_t(lo(0))) * int16_t(lo(5)));
  D(1) = uint32_t(int32_t(int16_t(lo(1))) * int16_t(lo(6)));
  D(2) = uint32_t(int32_t(int16_t(lo(2))) * int16_t(lo(7)));
  D(2) += D(0);
  D(2) += D(1);
  if (int32_t(D(2)) < 0) negl(2);
  D(2) = uint32_t(asr32(int32_t(D(2)), 4));
  A(3) = 0x34064;
  if (!(sl(0x5ad70) > -128)) A(3) = 0x3407c;
  setw(0, RW(-40));
  setw(0, asr16(lo(0), 4));
  if (int16_t(lo(0)) > 10) setw(0, 11);
  setw(0, lo(0) + lo(0));
  setw(1, rd16(A(3) + uint32_t(e16(lo(0)))));
  if (int16_t(lo(2)) < int16_t(lo(1))) return ret(-1);
  ret(0);
}

// $34094: two fans of interpolated spans between three vertices
void op_34094() {
  setw(0, 15);
  setw(1, 0xFFFF);
  setw(2, 0);
  setw(3, 0);
  mstore(0x5a9de, {0, 1, 2, 3});
  wr16(0x5ac4c, next16());
  WW(-126, 0);
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  WW(-56, rd16(A(2)));
  WW(-58, rd16(A(2)));
  A(2) += 2;
  push_a1_a2_a5();
  movemw(A(3), 1, 6);
  A(3) += 12;
  setw(1, lo(1) - rd16(A(3)));
  setw(2, lo(2) - rd16(A(3) + 2));
  setw(3, lo(3) - rd16(A(3) + 4));
  setw(4, lo(4) - rd16(A(3)));
  setw(5, lo(5) - rd16(A(3) + 2));
  setw(6, lo(6) - rd16(A(3) + 4));
  mstore(FP() - 70, {1, 2, 3, 4, 5, 6});
  movemw(FP() - 64, 4, 3);
  setw(4, asr16(lo(4), 1));
  setw(5, asr16(lo(5), 1));
  setw(6, asr16(lo(6), 1));
  movemw(A(3), 1, 3);
  setw(1, lo(1) - lo(4));
  setw(2, lo(2) - lo(5));
  setw(3, lo(3) - lo(6));
  setw(1, lo(1) + RW(-70));
  setw(2, lo(2) + RW(-68));
  setw(3, lo(3) + RW(-66));
  mstore(FP() - 76, {1, 2, 3});
  D(4) = D(5) = D(6) = 0;
  for (;;) {                                          // $34126
    mstore(FP() - 88, {4, 5, 6});
    setw(4, asr16(lo(4), 1));
    setw(5, asr16(lo(5), 1));
    setw(6, asr16(lo(6), 1));
    movemw(A(3), 1, 3);
    setw(1, lo(1) + lo(4));
    setw(2, lo(2) + lo(5));
    setw(3, lo(3) + lo(6));
    setw(4, lo(1));
    setw(5, lo(2));
    setw(6, lo(3));
    setw(4, lo(4) + RW(-70));
    setw(5, lo(5) + RW(-68));
    setw(6, lo(6) + RW(-66));
    mstore(0x61886, {1, 2, 3, 4, 5, 6});
    cpu().push32(A(3));
    call_c(0x43a56);
    WW(-126, RW(-126) | lo(0));
    A(3) = cpu().pop32();
    WW(-56, RW(-56) - 1);
    if (!(int16_t(RW(-56)) > 0)) break;
    movemw(FP() - 88, 4, 3);                          // $34114
    setw(4, lo(4) + RW(-64));
    setw(5, lo(5) + RW(-62));
    setw(6, lo(6) + RW(-60));
  }
  D(4) = D(5) = D(6) = 0;
  for (;;) {                                          // $34184
    mstore(FP() - 88, {4, 5, 6});
    setw(4, asr16(lo(4), 1));
    setw(5, asr16(lo(5), 1));
    setw(6, asr16(lo(6), 1));
    movemw(FP() - 76, 1, 3);
    setw(1, lo(1) - lo(4));
    setw(2, lo(2) - lo(5));
    setw(3, lo(3) - lo(6));
    setw(4, lo(1));
    setw(5, lo(2));
    setw(6, lo(3));
    setw(4, lo(4) - RW(-70));
    setw(5, lo(5) - RW(-68));
    setw(6, lo(6) - RW(-66));
    mstore(0x61886, {1, 2, 3, 4, 5, 6});
    call_c(0x43a56);
    WW(-126, RW(-126) | lo(0));
    WW(-58, RW(-58) - 1);
    if (!(int16_t(RW(-58)) > 0)) break;
    movemw(FP() - 88, 4, 3);                          // $34172
    setw(4, lo(4) + RW(-64));
    setw(5, lo(5) + RW(-62));
    setw(6, lo(6) + RW(-60));
  }
  pop_a1_a2_a5();
  setw(0, RW(-126));
}

}  // namespace

NATIVE_ASM(0x335b0, op_335b0, true);
NATIVE_ASM(0x335b4, op_335b4, true);
NATIVE_ASM(0x335d6, op_335d6, true);
NATIVE_ASM(0x335f8, op_335f8, true);
NATIVE_ASM(0x33664, op_33664, true);
NATIVE_ASM(0x33674, op_33674, true);
NATIVE_ASM(0x33682, op_33682, true);
NATIVE_ASM(0x3369a, op_3369a, true);
NATIVE_ASM(0x336d6, op_336d6, true);
NATIVE_ASM(0x3372c, op_3372c, true);
NATIVE_ASM(0x33734, op_33734, true);
NATIVE_ASM(0x33744, op_33744, true);
NATIVE_ASM(0x33792, op_33792, true);
NATIVE_ASM(0x337ec, op_337ec, true);
NATIVE_ASM(0x33890, op_33890, true);
NATIVE_ASM(0x33936, op_33936, true);
NATIVE_ASM(0x33b5c, op_33b5c, true);
NATIVE_ASM(0x33b60, op_33b60, true);
NATIVE_ASM(0x33d22, op_33d22, true);
NATIVE_ASM(0x33de6, op_33de6, true);
NATIVE_ASM(0x33e74, op_33e74, true);
NATIVE_ASM(0x33f8e, op_33f8e, true);
NATIVE_ASM(0x33fb6, op_33fb6, true);
NATIVE_ASM(0x33fba, op_33fba, true);
NATIVE_ASM(0x34094, op_34094, true);

}  // namespace native
