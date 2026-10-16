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
// movew fp@(-126),d0 as the last instruction: N/Z come from the word
void ret_fp126() {
  setw(0, RW(-126));
  flags(RW(-126), 16);
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
  ret_fp126();
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
  ret_fp126();
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
  ret_fp126();
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
  ret_fp126();
}

// ---- $341d0-$34a26: fans, strips and vertex-list ops ----------------------------
void sub_words(int r0, int n, uint32_t a) {           // subw (a)+... d<r0>..
  for (int i = 0; i < n; ++i) setw(r0 + i, lo(r0 + i) - rd16(a + 2u * uint32_t(i)));
}
void add_words_frame(int r0, int n, int32_t off) {    // addw fp@(off),d<r0>..
  for (int i = 0; i < n; ++i) setw(r0 + i, lo(r0 + i) + RW(off + 2 * i));
}
void sub_words_frame(int r0, int n, int32_t off) {
  for (int i = 0; i < n; ++i) setw(r0 + i, lo(r0 + i) - RW(off + 2 * i));
}
void asr1(int r0, int n) {
  for (int i = 0; i < n; ++i) setw(r0 + i, asr16(lo(r0 + i), 1));
}
void add_regs(int dst, int src, int n) {              // addw d<src>,d<dst>
  for (int i = 0; i < n; ++i) setw(dst + i, lo(dst + i) + lo(src + i));
}
void sub_regs(int dst, int src, int n) {
  for (int i = 0; i < n; ++i) setw(dst + i, lo(dst + i) - lo(src + i));
}
void copy_regs(int dst, int src, int n) {
  for (int i = 0; i < n; ++i) setw(dst + i, lo(src + i));
}
void or_result() { WW(-126, RW(-126) | lo(0)); }
void set_5a9de(uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
  setw(0, a);
  setw(1, b);
  setw(2, c);
  setw(3, d);
  mstore(0x5a9de, {0, 1, 2, 3});
}

void fan_34200() {
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  WW(-56, rd16(A(2)));
  WW(-58, rd16(A(2)));
  A(2) += 2;
  push_a1_a2_a5();
  movemw(A(3), 1, 6);
  A(3) += 12;
  A(4) = A(3) + 6;
  sub_words(1, 3, A(3));
  sub_words(4, 3, A(3));
  mstore(FP() - 70, {1, 2, 3, 4, 5, 6});
  asr1(4, 3);
  movemw(A(3), 1, 3);
  sub_regs(1, 4, 3);
  add_words_frame(1, 3, -70);
  mstore(FP() - 76, {1, 2, 3});
  movemw(A(4), 1, 3);
  sub_regs(1, 4, 3);
  add_words_frame(1, 3, -70);
  mstore(FP() - 82, {1, 2, 3});
  WW(-126, 0);
  A(0) = 0x61284;
  wr32(A(0), 4);
  A(0) += 4;
  setw(4, 0);
  setw(5, 0);
  setw(6, 0);
  for (;;) {
    mstore(FP() - 88, {4, 5, 6});                     // $3429e
    A(0) = 0x61288;
    movemw(A(3), 1, 3);
    asr1(4, 3);
    add_regs(1, 4, 3);
    copy_regs(4, 1, 3);
    add_words_frame(4, 3, -70);
    mstore(A(0), {1, 2, 3, 4, 5, 6});
    A(0) += 12;
    movemw(A(4), 4, 3);
    movemw(FP() - 88, 1, 3);
    asr1(1, 3);
    add_regs(4, 1, 3);
    copy_regs(1, 4, 3);
    add_words_frame(1, 3, -70);
    mstore(A(0), {1, 2, 3, 4, 5, 6});
    cpu().push32(A(4));
    cpu().push32(A(3));
    call_c(0x38c0c);
    or_result();
    A(3) = cpu().pop32();
    A(4) = cpu().pop32();
    WW(-56, RW(-56) - 1);
    if (!(int16_t(RW(-56)) > 0)) break;
    movemw(FP() - 88, 4, 3);                          // $3428c
    add_words_frame(4, 3, -64);
  }
  setw(4, 0);
  setw(5, 0);
  setw(6, 0);
  for (;;) {
    mstore(FP() - 88, {4, 5, 6});                     // $34334
    A(0) = 0x61288;
    movemw(FP() - 76, 1, 3);
    asr1(4, 3);
    sub_regs(1, 4, 3);
    copy_regs(4, 1, 3);
    sub_words_frame(4, 3, -70);
    mstore(A(0), {1, 2, 3, 4, 5, 6});
    A(0) += 12;
    movemw(FP() - 82, 4, 3);
    movemw(FP() - 88, 1, 3);
    asr1(1, 3);
    sub_regs(4, 1, 3);
    copy_regs(1, 4, 3);
    sub_words_frame(1, 3, -70);
    mstore(A(0), {1, 2, 3, 4, 5, 6});
    call_c(0x38c0c);
    WW(-58, RW(-58) - 1);
    if (!(int16_t(RW(-58)) > 0)) break;
    movemw(FP() - 88, 4, 3);                          // $34322
    add_words_frame(4, 3, -64);
  }
  pop_a1_a2_a5();
  ret_fp126();
}
void op_341d0() {
  wr32(0x5a9de, 0x000FFFFF);
  wr16(0x5ac4c, next16());
  fan_34200();
}
void op_341e2() {
  wr16(0x5ac4c, 13);
  set_5a9de(2, 0, 2, 0);
  fan_34200();
}

void strip_343ea() {
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  WW(-56, next16());
  movemw(A(3), 1, 6);
  A(3) += 12;
  sub_words(1, 3, A(3) + 6);
  sub_words(4, 3, A(3) + 6);
  mstore(FP() - 70, {1, 2, 3, 4, 5, 6});
  movemw(A(3), 1, 3);
  A(3) += 6;
  sub_words(1, 3, A(3));
  mstore(FP() - 82, {1, 2, 3});
  WW(-126, 0);
  A(0) = 0x61284;
  wr32(A(0), 4);
  A(0) += 4;
  for (;;) {
    WW(-58, next16());                                // $34440
    setw(4, 0);
    setw(5, 0);
    setw(6, 0);
    for (;;) {
      mstore(FP() - 88, {4, 5, 6});                   // $3445e
      A(0) = 0x61288;
      movemw(A(3), 1, 3);
      asr1(4, 3);
      add_regs(1, 4, 3);
      copy_regs(4, 1, 3);
      add_words_frame(4, 3, -70);
      mstore(A(0), {1, 2, 3, 4, 5, 6});
      A(0) += 12;
      movemw(A(3), 4, 3);
      add_words_frame(4, 3, -82);
      movemw(FP() - 88, 1, 3);
      asr1(1, 3);
      add_regs(4, 1, 3);
      copy_regs(1, 4, 3);
      add_words_frame(1, 3, -70);
      mstore(A(0), {1, 2, 3, 4, 5, 6});
      cpu().push32(A(3));
      cpu().push32(A(2));
      call_c(0x38c0c);
      or_result();
      A(2) = cpu().pop32();
      A(3) = cpu().pop32();
      WW(-58, RW(-58) - 1);
      if (!(int16_t(RW(-58)) > 0)) break;
      movemw(FP() - 88, 4, 3);                        // $3444c
      add_words_frame(4, 3, -64);
    }
    WW(-56, RW(-56) - 1);
    if (!(int16_t(RW(-56)) > 0)) break;
    A(3) += 6;                                        // $3443e
  }
  pop_a1_a5();
  ret_fp126();
}
void op_343b2() {
  push_a1_a5();
  wr32(0x5a9de, 0x000FFFFF);
  wr16(0x5ac4c, next16());
  strip_343ea();
}
void op_343c8() {
  push_a1_a5();
  wr16(0x5ac4c, 13);
  set_5a9de(2, 0, 2, 0);
  strip_343ea();
}

void op_344f8() {
  push_a1_a5();
  set_5a9de(15, 0xFFFF, 0, 0);
  wr16(0x5ac4c, next16());
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  WW(-56, next16());
  movemw(A(3), 1, 6);
  A(3) += 12;
  sub_words(1, 3, A(3));
  mstore(FP() - 64, {1, 2, 3});
  sub_words(4, 3, A(3));
  mstore(FP() - 82, {4, 5, 6});
  WW(-126, 0);
  for (;;) {
    WW(-58, next16());                                // $3454e
    setw(4, 0);
    setw(5, 0);
    setw(6, 0);
    for (;;) {
      mstore(FP() - 88, {4, 5, 6});                   // $3456c
      movemw(A(3), 1, 3);
      asr1(4, 3);
      add_regs(1, 4, 3);
      mstore(0x61886, {1, 2, 3});
      movemw(A(3), 1, 3);
      add_words_frame(1, 3, -82);
      add_regs(1, 4, 3);
      mstore(0x6188c, {1, 2, 3});
      cpu().push32(A(3));
      cpu().push32(A(2));
      call_c(0x43a56);
      or_result();
      A(2) = cpu().pop32();
      A(3) = cpu().pop32();
      WW(-58, RW(-58) - 1);
      if (!(int16_t(RW(-58)) > 0)) break;
      movemw(FP() - 88, 4, 3);                        // $3455a
      add_words_frame(4, 3, -64);
    }
    WW(-56, RW(-56) - 1);
    if (!(int16_t(RW(-56)) > 0)) break;
    A(3) += 6;                                        // $3454c
  }
  pop_a1_a5();
  ret_fp126();
}

// $345d0 / $345de: one quad from three vertices (v0, v1, v1+v2-v0)
void quad_345e8() {
  setw(3, 0);
  mstore(0x5a9de, {0, 1, 2, 3});
  wr16(0x5ac4c, next16());
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  push_a1_a2_a5();
  A(0) = 0x61284;
  wr32(A(0), 4);
  A(0) += 4;
  movemw(A(3), 2, 6);
  A(3) += 12;
  mstore(A(0), {2, 3, 4, 5, 6, 7});
  setw(0, lo(4));
  setw(0, lo(0) & lo(7));
  A(0) += 12;
  setw(5, lo(5) - lo(2));
  setw(6, lo(6) - lo(3));
  setw(7, lo(7) - lo(4));
  movemw(A(3), 2, 3);
  setw(0, lo(0) & lo(4));
  mstore(A(0), {2, 3, 4});
  A(0) += 6;
  setw(2, lo(2) - lo(5));
  setw(3, lo(3) - lo(6));
  setw(4, lo(4) - lo(7));
  setw(0, lo(0) & lo(4));
  if (int16_t(lo(0)) < 0) {
    pop_a1_a2_a5();
    return ret(0);
  }
  mstore(A(0), {2, 3, 4});
  call_c(0x38c0c);
  pop_a1_a2_a5();
}
void op_345d0() {
  setw(0, 2);
  setw(1, 0);
  setw(2, 2);
  quad_345e8();
}
void op_345de() {
  setw(0, 8);
  setw(1, 8);
  setw(2, 0);
  quad_345e8();
}

// $34654-$347de: vertex-list transforms that edit the table at a3 in place
void shift_d345(int cnt_reg) {                        // asw/asl d3-d5 by count
  if (int16_t(lo(cnt_reg)) < 0) {
    negw(cnt_reg);
    for (int r = 3; r <= 5; ++r) setw(r, asr16(lo(r), D(cnt_reg)));
  } else {
    for (int r = 3; r <= 5; ++r) setw(r, asl16(lo(r), D(cnt_reg)));
  }
}
void a4_sub(int r) { A(4) -= uint32_t(e16(lo(r))); }
void a4_add(int r) { A(4) += uint32_t(e16(lo(r))); }
void head_34654(uint32_t off, uint32_t st1, uint32_t st2) {
  movemw(A(3) + off, 0, 8);
  A(4) = uint32_t(e16(rd16(A(3) + off + 16)));
  setw(0, lo(0) - lo(3));
  setw(1, lo(1) - lo(4));
  setw(2, lo(2) - lo(5));
  setw(3, lo(3) - lo(6));
  setw(4, lo(4) - lo(7));
  setw(5, lo(5) - uint16_t(A(4)));
  swapw(0);
  setw(0, next16());
  shift_d345(0);
  swapw(0);
  setw(6, lo(6) - lo(3));
  setw(7, lo(7) - lo(4));
  a4_sub(5);
  mstore(A(3) + st1, {6, 7, 12});
  setw(6, lo(6) - lo(0));
  setw(7, lo(7) - lo(1));
  a4_sub(2);
  mstore(A(3) + st2, {6, 7, 12});
}
void step_6(uint32_t store_off) {                     // movemw (a3)+,d6-d7/a4; +d0..d2
  movemw(A(3), 6, 2);
  A(4) = uint32_t(e16(rd16(A(3) + 4)));
  A(3) += 6;
  setw(6, lo(6) + lo(0));
  setw(7, lo(7) + lo(1));
  a4_add(2);
  mstore(A(3) + store_off, {6, 7, 12});
}
void op_346a8() {
  movemw(A(3), 0, 8);
  A(4) = uint32_t(e16(rd16(A(3) + 16)));
  A(3) += 18;
  setw(0, lo(0) - lo(3));
  setw(1, lo(1) - lo(4));
  setw(2, lo(2) - lo(5));
  setw(6, lo(6) + lo(0));
  setw(7, lo(7) + lo(1));
  a4_add(2);
  mstore(A(3) + 6, {6, 7, 12});
  step_6(6);
  swapw(0);
  swapw(1);
  swapw(2);
  setw(0, lo(3));
  setw(1, lo(4));
  setw(2, lo(5));
  movemw(A(3) - 12, 3, 5);
  A(4) = uint32_t(e16(rd16(A(3) - 12 + 10)));
  setw(3, lo(3) - lo(0));
  setw(4, lo(4) - lo(1));
  setw(5, lo(5) - lo(2));
  setw(6, lo(6) + lo(3));
  setw(7, lo(7) + lo(4));
  a4_add(5);
  mstore(A(3) + 12, {6, 7, 12});
  swapw(0);
  swapw(1);
  swapw(2);
  setw(6, lo(6) + lo(0));
  setw(7, lo(7) + lo(1));
  a4_add(2);
  mstore(A(3) + 18, {6, 7, 12});
  ret(0);
}
void op_34654() {
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  head_34654(6, 48, 54);
  op_346a8();
}
void op_346a0() {
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  op_346a8();
}
void tail_3475c() {
  movemw(A(3), 0, 8);
  A(4) = uint32_t(e16(rd16(A(3) + 16)));
  A(3) += 18;
  setw(0, lo(0) - lo(3));
  setw(1, lo(1) - lo(4));
  setw(2, lo(2) - lo(5));
  setw(6, lo(6) + lo(0));
  setw(7, lo(7) + lo(1));
  a4_add(2);
  mstore(A(3) + 18, {6, 7, 12});
  for (int i = 0; i < 3; ++i) step_6(18);
  swapw(0);
  swapw(1);
  swapw(2);
  setw(0, lo(3));
  setw(1, lo(4));
  setw(2, lo(5));
  movemw(A(3) - 24, 3, 3);
  movemw(A(3) - 6, 6, 2);
  A(4) = uint32_t(e16(rd16(A(3) - 6 + 4)));
  setw(3, lo(3) - lo(0));
  setw(4, lo(4) - lo(1));
  setw(5, lo(5) - lo(2));
  setw(6, lo(6) + lo(3));
  setw(7, lo(7) + lo(4));
  a4_add(5);
  mstore(A(3) + 24, {6, 7, 12});
  swapw(0);
  swapw(1);
  swapw(2);
  setw(6, lo(6) + lo(0));
  setw(7, lo(7) + lo(1));
  a4_add(2);
  mstore(A(3) + 30, {6, 7, 12});
  ret(0);
}
void op_34708() {
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  head_34654(12, 72, 78);
  tail_3475c();
}
void op_34754() {
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  tail_3475c();
}

// $347ec: list of quads (four vertex indices each, list ends with a negative word)
void op_347ec() {
  push_a1_a5();
  wr16(0x5ac4c, 12);
  set_5a9de(8, 8, 0, 0);
  WW(-126, 0);
  wr32(0x61284, 4);
  for (;;) {
    setw(1, next16());                                // $3481a
    if (int16_t(lo(1)) < 0) break;
    movemw(A(2), 2, 3);
    A(2) += 6;
    A(0) = 0x61288;
    A(3) = 0x5d684;
    for (int i = 1; i <= 4; ++i) {
      A(4) = A(3) + uint32_t(e16(lo(i)));
      wr32(A(0), rd32(A(4)));
      A(0) += 4;
      A(4) += 4;
      wr16(A(0), rd16(A(4)));
      A(0) += 2;
      if (i == 1) setw(6, rd16(A(4)));
      else setw(6, lo(6) & rd16(A(4)));
    }
    if (int16_t(lo(6)) < 0) continue;
    cpu().push32(A(2));
    call_c(0x38c0c);
    or_result();
    A(2) = cpu().pop32();
  }
  pop_a1_a5();
  ret_fp126();
}

// $34872: strip of parallelograms along an extrusion vector
void op_34872() {
  set_5a9de(2, 0, 2, 0);
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  WW(-56, next16());
  push_a1_a2_a5();
  movemw(A(3), 1, 6);
  A(3) += 12;
  sub_words(1, 3, A(3) + 6);
  sub_words(4, 3, A(3) + 6);
  for (int r = 4; r <= 6; ++r) {
    setw(7, lo(r));
    setw(7, lo(7) + lo(7));
    setw(r, lo(r) + lo(7));
  }
  asr1(4, 3);
  add_regs(4, 1, 3);
  mstore(FP() - 70, {4, 5, 6});
  movemw(A(3), 1, 3);
  A(3) += 6;
  sub_words(1, 3, A(3));
  mstore(FP() - 82, {1, 2, 3});
  WW(-126, 0);
  A(0) = 0x61284;
  wr32(A(0), 4);
  A(0) += 4;
  for (;;) {
    A(0) = 0x61288;                                   // $34900
    movemw(A(3), 1, 3);
    copy_regs(4, 1, 3);
    add_words_frame(4, 3, -70);
    mstore(A(0), {1, 2, 3, 4, 5, 6});
    A(0) += 12;
    movemw(A(3), 4, 3);
    add_words_frame(4, 3, -82);
    copy_regs(1, 4, 3);
    add_words_frame(1, 3, -70);
    mstore(A(0), {1, 2, 3, 4, 5, 6});
    cpu().push32(A(3));
    call_c(0x38c0c);
    or_result();
    A(3) = cpu().pop32();
    WW(-56, RW(-56) - 1);
    if (!(int16_t(RW(-56)) > 0)) break;
    A(3) += 6;                                        // $348fe
  }
  pop_a1_a2_a5();
  ret_fp126();
}

// $34968: n triangles/quads copied straight from the vertex table
void op_34968() {
  setw(0, next16());
  setw(1, lo(0) & 63);
  wr16(0x5ac4c, lo(1));
  setw(0, lo(0) >> 8);
  WW(-48, lo(0));
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  push_a1_a2_a5();
  WW(-126, 0);
  do {
    movemw(A(3), 0, 6);                               // $3498c
    A(3) += 12;
    mstore(0x61886, {0, 1, 2, 3, 4, 5});
    cpu().push32(A(3));
    call_c(0x43a56);
    or_result();
    A(3) = cpu().pop32();
    WW(-48, RW(-48) - 1);
  } while (int16_t(RW(-48)) > 0);
  pop_a1_a2_a5();
  ret_fp126();
}
void op_349b6() {
  setw(0, next16());
  setw(1, lo(0) & 63);
  wr16(0x5ac4c, lo(1));
  setw(0, lo(0) >> 8);
  WW(-48, lo(0));
  setw(0, next16());
  A(3) = 0x5d684;
  movemw(A(3) + uint32_t(e16(lo(0))), 2, 6);
  setw(5, lo(5) - lo(2));
  setw(6, lo(6) - lo(3));
  setw(7, lo(7) - lo(4));
  A(4) = uint32_t(e16(lo(5)));
  A(3) += uint32_t(e16(next16()));
  push_a1_a2_a5();
  WW(-126, 0);
  do {
    movemw(A(3), 0, 6);                               // $349ea
    A(3) += 12;
    setw(0, lo(0) - uint16_t(A(4)));
    setw(1, lo(1) - lo(6));
    setw(2, lo(2) - lo(7));
    setw(3, lo(3) - uint16_t(A(4)));
    setw(4, lo(4) - lo(6));
    setw(5, lo(5) - lo(7));
    mstore(0x61886, {0, 1, 2, 3, 4, 5});
    cpu().push32(A(3));
    cpu().a[7] -= 6;
    wr16(cpu().a[7], lo(6));
    wr16(cpu().a[7] + 2, lo(7));
    wr16(cpu().a[7] + 4, uint16_t(A(4)));
    call_c(0x43a56);
    or_result();
    movemw(cpu().a[7], 6, 2);
    A(4) = uint32_t(e16(rd16(cpu().a[7] + 4)));
    cpu().a[7] += 6;
    A(3) = cpu().pop32();
    WW(-48, RW(-48) - 1);
  } while (int16_t(RW(-48)) > 0);
  pop_a1_a2_a5();
  ret_fp126();
}

// ---- $34a28-$353d8: line / quad / object-dependent ops --------------------------
void st3(int r) {                                     // movew d<r>,(a0)+ x3
  for (int i = 0; i < 3; ++i) {
    wr16(A(0), lo(r + i));
    A(0) += 2;
  }
}
// movel a3(0,d),(a0)+ ; movew a3(4,d),(a0)+  (advance_last: the word too)
void put_vertex(uint32_t idx, bool advance_last) {
  wr32(A(0), rd32(A(3) + idx));
  A(0) += 4;
  wr16(A(0), rd16(A(3) + idx + 4));
  if (advance_last) A(0) += 2;
}
uint32_t X(int r) { return uint32_t(e16(lo(r))); }   // index register as offset
void sub3(int dst, int src) {                         // subw d<src>,d<dst> x3
  sub_regs(dst, src, 3);
}
void ret0_pop_a1_a2_a5() {
  pop_a1_a2_a5();
  ret(0);
}

void add_words_a3(int r0, int n, uint32_t off) {       // addw a3@(off),d<r0>..
  for (int i = 0; i < n; ++i) setw(r0 + i, lo(r0 + i) + rd16(A(3) + off + 2u * uint32_t(i)));
}

void op_34a3c() {
  push_a1_a5();
  wr32(0x5a9de, 0xFFFFFFFFu);
  wr16(0x5ac4c, next16());
  WW(-126, 0);
  WW(-110, 0);
  while (RW(-110) == 0) {                             // $34a58
    setw(1, next16());
    setw(2, next16());
    if (int16_t(lo(2)) < 0) {
      WW(-110, RW(-110) + 1);
      setw(2, lo(2) & 0x7FFF);
    }
    A(3) = 0x5d684;
    A(0) = 0x61886;
    A(4) = A(3) + X(1);
    wr32(A(0), rd32(A(4)));
    A(0) += 4;
    A(4) += 4;
    setw(6, rd16(A(4)));
    wr16(A(0), lo(6));
    A(0) += 2;
    A(4) = A(3) + X(2);
    wr32(A(0), rd32(A(4)));
    A(0) += 4;
    A(4) += 4;
    setw(6, lo(6) & rd16(A(4)));
    if (int16_t(lo(6)) < 0) continue;
    wr16(A(0), rd16(A(4)));
    cpu().push32(A(2));
    call_c(0x43a56);
    or_result();
    A(2) = cpu().pop32();
  }
  pop_a1_a5();
  ret_fp126();
}
void op_34a28() {
  if (sl(0x5ad70) < -192) return op_34a3c();
  do {
    A(2) += 2;
  } while (int16_t(rd16(A(2) - 2)) >= 0);
  ret(0);
}
void op_34aa8() {
  push_a1_a5();
  wr16(0x5ac4c, next16());
  setw(0, next16());
  WW(-126, 0);
  WW(-110, 0);
  while (RW(-110) == 0) {                             // $34abc
    setw(1, next16());
    if (int16_t(rd16(A(2))) < 0) WW(-110, RW(-110) + 1);
    A(3) = 0x5d684;
    movemw(A(3) + X(0), 2, 6);
    setw(5, lo(5) - lo(2));
    setw(6, lo(6) - lo(3));
    setw(7, lo(7) - lo(4));
    A(0) = 0x61886;
    movemw(A(3) + X(1), 2, 3);
    setw(2, lo(2) - lo(5));
    setw(3, lo(3) - lo(6));
    setw(4, lo(4) - lo(7));
    mstore(A(0), {2, 3, 4});
    setw(1, next16());
    setw(1, lo(1) & 0x7FFF);
    movemw(A(3) + X(1), 2, 3);
    setw(2, lo(2) - lo(5));
    setw(3, lo(3) - lo(6));
    setw(4, lo(4) - lo(7));
    mstore(A(0) + 6, {2, 3, 4});
    cpu().push32(A(2));
    cpu().a[7] -= 2;
    wr16(cpu().a[7], lo(0));
    call_c(0x43a56);
    or_result();
    setw(0, rd16(cpu().a[7]));
    cpu().a[7] += 2;
    A(2) = cpu().pop32();
  }
  pop_a1_a5();
  ret_fp126();
}
void op_34b2a() {
  wr16(0x5ac4c, next16());
  movemw(A(2), 0, 3);
  setw(7, rd16(A(2) + 6));
  D(7) = uint32_t(e16(lo(7)));
  A(2) += 8;
  A(3) = 0x5d684;
  A(0) = 0x61284;
  wr16(A(0), 0);
  A(0) += 2;
  wr16(A(0), 4);
  A(0) += 2;
  put_vertex(X(1), true);
  setw(6, rd16(A(0) - 2));
  put_vertex(X(2), true);
  setw(6, lo(6) & rd16(A(0) - 2));
  put_vertex(X(7), true);
  setw(6, lo(6) & rd16(A(0) - 2));
  movemw(A(3) + X(0), 0, 6);
  setw(3, lo(3) - lo(0));
  setw(4, lo(4) - lo(1));
  setw(5, lo(5) - lo(2));
  movemw(A(3) + X(7), 0, 3);
  setw(0, lo(0) - lo(3));
  setw(1, lo(1) - lo(4));
  setw(2, lo(2) - lo(5));
  mstore(A(0), {0, 1, 2});
  setw(6, lo(6) & lo(2));
  if (int16_t(lo(6)) < 0) return ret(0);
  push_a1_a2_a5();
  call_c(0x38c0a);
  pop_a1_a2_a5();
}
void op_34b9e() {
  wr16(0x5ac4c, next16());
  setw(7, next16());
  A(3) = 0x5d684;
  A(0) = 0x61284;
  wr16(A(0), 0);
  A(0) += 2;
  wr16(A(0), 4);
  A(0) += 2;
  put_vertex(X(7), true);
  setw(6, rd16(A(0) - 2));
  setw(0, next16());
  movemw(A(3) + X(0), 0, 6);
  setw(3, lo(3) - lo(0));
  setw(4, lo(4) - lo(1));
  setw(5, lo(5) - lo(2));
  movemw(A(3) + X(7), 0, 3);
  setw(0, lo(0) - lo(3));
  setw(1, lo(1) - lo(4));
  setw(2, lo(2) - lo(5));
  st3(0);
  setw(6, lo(6) & lo(2));
  setw(7, next16());
  movemw(A(3) + X(7), 0, 3);
  setw(0, lo(0) - lo(3));
  setw(1, lo(1) - lo(4));
  setw(2, lo(2) - lo(5));
  st3(0);
  setw(6, lo(6) & lo(2));
  put_vertex(X(7), false);
  setw(6, lo(6) & rd16(A(0)));
  if (int16_t(lo(6)) < 0) return ret(0);
  push_a1_a2_a5();
  call_c(0x38c0a);
  pop_a1_a2_a5();
}
void op_34c1c() {
  wr16(0x5ac4c, next16());
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  if (sl(0x5ad70) < -128) return ret(0);
  A(0) = 0x61284;
  wr16(A(0), 0);
  A(0) += 2;
  wr16(A(0), 4);
  A(0) += 2;
  movemw(A(3), 2, 6);
  A(3) += 12;
  mstore(A(0), {2, 3, 4, 5, 6, 7});
  A(0) += 12;
  setw(5, lo(5) - lo(2));
  setw(6, lo(6) - lo(3));
  setw(7, lo(7) - lo(4));
  movemw(A(3), 2, 3);
  st3(2);
  setw(2, lo(2) - lo(5));
  setw(3, lo(3) - lo(6));
  setw(4, lo(4) - lo(7));
  mstore(A(0), {2, 3, 4});
  setw(7, lo(4));
  setw(7, lo(7) & rd16(A(0) + 10));
  setw(7, lo(7) & rd16(A(0) + 16));
  setw(7, lo(7) & rd16(A(0) + 22));
  if (int16_t(lo(7)) < 0) return ret(0);
  push_a1_a2_a5();
  call_c(0x38c0a);
  pop_a1_a2_a5();
}
void op_34c8c() {
  wr16(0x5ac4c, next16());
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  if (sl(0x5ad70) < -128) return ret(0);
  push_a1_a2_a5();
  A(0) = 0x61284;
  wr16(A(0), 0);
  A(0) += 2;
  wr16(A(0), 4);
  A(0) += 2;
  movemw(A(3), 2, 6);
  setw(5, lo(5) - lo(2));
  setw(6, lo(6) - lo(3));
  setw(7, lo(7) - lo(4));
  A(1) = uint32_t(e16(lo(5)));
  A(4) = uint32_t(e16(lo(6)));
  A(5) = uint32_t(e16(lo(7)));
  movemw(A(3) + 18, 0, 3);
  st3(0);
  add_regs(0, 5, 3);
  st3(0);
  A(2) = uint32_t(e16(lo(2)));
  movemw(A(3) + 6, 2, 6);
  setw(5, lo(5) - lo(2));
  setw(6, lo(6) - lo(3));
  setw(7, lo(7) - lo(4));
  setw(2, uint16_t(A(2)));
  add_regs(0, 5, 3);
  st3(0);
  setw(0, lo(0) - uint16_t(A(1)));
  setw(1, lo(1) - uint16_t(A(4)));
  setw(2, lo(2) - uint16_t(A(5)));
  mstore(A(0), {0, 1, 2});
  setw(7, rd16(A(0) - 14));
  setw(7, lo(7) & rd16(A(0) - 8));
  setw(7, lo(7) & rd16(A(0) - 2));
  setw(7, lo(7) & rd16(A(0) + 4));
  if (int16_t(lo(7)) < 0) return ret0_pop_a1_a2_a5();
  call_c(0x38c0a);
  pop_a1_a2_a5();
}

// shared by $34d2a/$34e16: view-space product d4 = x*dx + y*dy for the facing test
void facing_setup(uint32_t a4) {                      // d1,d2 deltas; d3,d4 projected
  setw(0, next16());
  setw(1, rd16(a4 + 10 + X(0)));
  setw(2, rd16(a4 + 14 + X(0)));
  setw(3, lo(1));
  setw(4, lo(2));
  setw(0, next16());
  setw(6, lo(0));
  setw(0, lo(0) & 0x7FFF);
  setw(1, lo(1) - rd16(a4 + 10 + X(0)));
  setw(2, lo(2) - rd16(a4 + 14 + X(0)));
  setb(7, rd8(a4 + 6));
  setw(7, lo(7) & 15);
  setw(3, asr16(lo(3), D(7)));
  setw(4, asr16(lo(4), D(7)));
  setw(3, lo(3) + rd16(0x5ae22));
  setw(4, lo(4) + rd16(0x5ae26));
  setw(7, rd16(0x5adb0));
  setw(3, asl16(lo(3), D(7)));
  setw(4, asl16(lo(4), D(7)));
  setw(3, lo(3) + rd16(0x5ad6a));
  setw(4, lo(4) + rd16(0x5ad6e));
  D(3) = uint32_t(int32_t(int16_t(lo(3))) * int16_t(lo(1)));
  D(4) = uint32_t(int32_t(int16_t(lo(4))) * int16_t(lo(2)));
}
void op_34d2a() {
  A(3) = 0x5d684;
  A(0) = 0x61286;
  wr16(A(0), 3);
  A(0) += 2;
  wr16(0x5ac4c, next16());
  A(4) = rd32(0x5ad2a);
  facing_setup(A(4));
  setw(0, next16());
  put_vertex(X(0), true);
  A(3) += uint32_t(e16(next16()));
  push_a1_a2_a5();
  int64_t sum = int64_t(int32_t(D(4))) + int32_t(D(3));
  D(4) += D(3);
  bool ge = sum >= 0, neg6 = int16_t(lo(6)) < 0;
  if (ge ? !neg6 : neg6) {                            // $34db4
    wr32(A(0), rd32(A(3)));
    A(0) += 4;
    wr16(A(0), rd16(A(3) + 4));
    A(0) += 2;
    wr32(A(0), rd32(A(3) + 12));
    A(0) += 4;
    wr16(A(0), rd16(A(3) + 16));
    setw(7, rd16(A(0) - 12));
    setw(7, lo(7) & rd16(A(0) - 6));
    setw(7, lo(7) & rd16(A(0)));
    if (int16_t(lo(7)) < 0) return ret0_pop_a1_a2_a5();
    call_c(0x38c0a);
    return pop_a1_a2_a5();
  }
  movemw(A(3), 2, 6);                                 // $34dda
  A(3) += 12;
  wr16(A(0), lo(5));
  wr16(A(0) + 2, lo(6));
  wr16(A(0) + 4, lo(7));
  A(0) += 6;
  setw(5, lo(5) - lo(2));
  setw(6, lo(6) - lo(3));
  setw(7, lo(7) - lo(4));
  movemw(A(3), 2, 3);
  setw(2, lo(2) - lo(5));
  setw(3, lo(3) - lo(6));
  setw(4, lo(4) - lo(7));
  mstore(A(0), {2, 3, 4});
  setw(4, lo(4) & rd16(A(0) - 8));
  setw(4, lo(4) & rd16(A(0) - 2));
  if (int16_t(lo(4)) < 0) return ret0_pop_a1_a2_a5();
  call_c(0x38c0a);
  pop_a1_a2_a5();
}
void op_34e16() {
  A(3) = 0x5d684;
  A(0) = 0x61286;
  wr16(A(0), 3);
  A(0) += 2;
  wr16(0x5ac4c, next16());
  setw(0, next16());
  movemw(A(3) + X(0), 2, 6);
  setw(5, lo(5) - lo(2));
  setw(6, lo(6) - lo(3));
  setw(7, lo(7) - lo(4));
  swapw(6);
  swapw(7);
  A(4) = rd32(0x5ad2a);
  facing_setup(A(4));
  D(4) += D(3);
  swapw(6);
  swapw(7);
  setw(0, next16());
  movemw(A(3) + X(0), 1, 3);
  setw(1, lo(1) - lo(5));
  setw(2, lo(2) - lo(6));
  setw(3, lo(3) - lo(7));
  st3(1);
  A(3) += uint32_t(e16(next16()));
  push_a1_a2_a5();
  bool far_path;                                      // true: $34f16
  if (int32_t(D(4)) >= 0) far_path = int32_t(D(6)) < 0;
  else far_path = !(int32_t(D(6)) < 0);
  if (!far_path) {                                    // $34eca
    movemw(A(3), 2, 6);
    setw(5, lo(5) - lo(2));
    setw(6, lo(6) - lo(3));
    setw(7, lo(7) - lo(4));
    movemw(A(3) + 18, 0, 3);
    st3(0);
    add_regs(0, 5, 3);
    A(2) = uint32_t(e16(lo(2)));
    movemw(A(3) + 6, 2, 6);
    setw(5, lo(5) - lo(2));
    setw(6, lo(6) - lo(3));
    setw(7, lo(7) - lo(4));
    setw(2, uint16_t(A(2)));
    add_regs(0, 5, 3);
    mstore(A(0), {0, 1, 2});
    setw(2, lo(2) & rd16(A(0) - 8));
    setw(2, lo(2) & rd16(A(0) - 2));
    if (int16_t(lo(2)) < 0) return ret0_pop_a1_a2_a5();
    call_c(0x38c0a);
    return pop_a1_a2_a5();
  }
  movemw(A(3), 2, 6);                                 // $34f16
  setw(5, lo(5) - lo(2));
  setw(6, lo(6) - lo(3));
  setw(7, lo(7) - lo(4));
  A(1) = uint32_t(e16(lo(5)));
  A(4) = uint32_t(e16(lo(6)));
  A(5) = uint32_t(e16(lo(7)));
  movemw(A(3) + 18, 0, 3);
  add_regs(0, 5, 3);
  st3(0);
  A(2) = uint32_t(e16(lo(2)));
  movemw(A(3) + 6, 2, 6);
  setw(5, lo(5) - lo(2));
  setw(6, lo(6) - lo(3));
  setw(7, lo(7) - lo(4));
  setw(2, uint16_t(A(2)));
  add_regs(0, 5, 3);
  setw(0, lo(0) - uint16_t(A(1)));
  setw(1, lo(1) - uint16_t(A(4)));
  setw(2, lo(2) - uint16_t(A(5)));
  mstore(A(0), {0, 1, 2});
  setw(2, lo(2) & rd16(A(0) - 8));
  setw(2, lo(2) & rd16(A(0) - 2));
  if (int16_t(lo(2)) < 0) return ret0_pop_a1_a2_a5();
  call_c(0x38c0a);
  pop_a1_a2_a5();
}

void op_34f76() {
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  movemw(A(3) + 6, 0, 8);
  A(4) = uint32_t(e16(rd16(A(3) + 6 + 16)));
  setw(3, lo(3) - lo(6));
  setw(4, lo(4) - lo(7));
  setw(5, lo(5) - uint16_t(A(4)));
  swapw(0);
  setw(0, next16());
  shift_d345(0);
  swapw(0);
  setw(6, lo(6) - lo(3));
  setw(7, lo(7) - lo(4));
  a4_sub(5);
  mstore(A(3) + 60, {6, 7, 12});
  setw(6, lo(6) + lo(3));
  setw(7, lo(7) + lo(4));
  a4_add(5);
  movemw(A(3) + 12, 3, 3);
  sub_regs(3, 0, 3);
  setw(6, lo(6) + lo(3));
  setw(7, lo(7) + lo(4));
  a4_add(5);
  mstore(A(3) + 54, {6, 7, 12});
  movemw(A(3) + 30, 0, 8);
  A(4) = uint32_t(e16(rd16(A(3) + 30 + 16)));
  sub_regs(0, 3, 3);
  setw(6, lo(6) + lo(0));
  setw(7, lo(7) + lo(1));
  a4_add(2);
  mstore(A(3) + 66, {6, 7, 12});
  movemw(A(3) + 48, 0, 3);
  sub_regs(0, 3, 3);
  setw(6, lo(6) + lo(0));
  setw(7, lo(7) + lo(1));
  a4_add(2);
  mstore(A(3) + 72, {6, 7, 12});
  movemw(A(3) + 24, 0, 3);
  sub_regs(0, 3, 3);
  movemw(A(3) + 30, 3, 3);
  add_regs(3, 0, 3);
  movemw(A(3) + 48, 6, 2);
  A(4) = uint32_t(e16(rd16(A(3) + 48 + 4)));
  setw(6, lo(6) + lo(0));
  setw(7, lo(7) + lo(1));
  a4_add(2);
  mstore(A(3) + 78, {3, 4, 5, 6, 7, 12});
  movemw(A(3) + 42, 3, 3);
  add_regs(3, 0, 3);
  mstore(A(3) + 90, {3, 4, 5});
  movemw(A(3) + 66, 3, 5);
  A(4) = uint32_t(e16(rd16(A(3) + 66 + 10)));
  add_regs(3, 0, 3);
  setw(6, lo(6) + lo(0));
  setw(7, lo(7) + lo(1));
  a4_add(2);
  mstore(A(3) + 96, {3, 4, 5, 6, 7, 12});
  ret(0);
}

// $35054-$35118: skip counts in the op stream selected by the object's state
uint32_t obj_5b47c() { return 0x5b47c + uint32_t(e16(rd16(0x5acae))); }
void op_35054() {
  A(3) = obj_5b47c();
  D(0) = 0;
  setb(1, rd8(A(3) + 124));
  setb(1, lob(1) & 15);
  auto dec = [] { setb(1, lob(1) - 1); return int8_t(lob(1)) < 0; };
  do {
    if (int8_t(lob(1)) > 6) break;
    if (dec()) break;
    D(0) = 20;
    if (dec()) break;
    D(0) = 82;
    if (dec()) break;
    setw(0, 124);
    if (dec()) break;
    setw(0, 166);
    if (dec()) break;
    setw(0, 208);
    if (dec()) break;
    setw(0, 250);
  } while (false);
  A(2) += uint32_t(e16(lo(0)));
  ret(0);
}
void op_350a2() {
  A(3) = obj_5b47c();
  D(0) = 0;
  setb(1, rd8(A(3) + 124));
  setb(1, lob(1) & 15);
  if (!(int8_t(lob(1)) < 3) && !(int8_t(lob(1)) > 6)) D(0) = 8;
  A(2) += uint32_t(e16(lo(0)));
  ret(0);
}
void op_350cc() {
  setw(2, next16());
  A(3) = obj_5b47c();
  setw(1, rd16(A(3) + 2));
  setw(1, lo(1) & 8);
  if (lo(1) == 0) D(2) = 0;
  A(2) += uint32_t(e16(lo(2)));
  ret(0);
}
void op_350ec() {
  A(2) += 2;
  ret(0);
}
void op_350f2() {
  A(3) = obj_5b47c();
  setb(1, rd8(0x5ab3f));
  setb(1, lob(1) & 0x7F);
  extw(1);
  setw(1, lo(1) + lo(1));
  setw(0, lo(1));
  setw(1, asl16(lo(1), 3));
  setw(1, lo(1) + lo(0));
  A(2) += uint32_t(e16(lo(1)));
  ret(0);
}
void op_35118() {
  A(3) = obj_5b47c();
  setb(1, rd8(A(3) + 124));
  setb(1, lob(1) & 0x7F);
  setb(1, uint32_t(int8_t(lob(1)) >> 4));
  if (!(int8_t(lob(1)) >= 6)) A(2) += 2;
  ret(0);
}
void op_3513a() {
  A(3) = 0x5d684;
  setw(0, next16());
  setw(1, next16());
  movemw(A(3) + X(0), 3, 3);
  movemw(A(3) + X(1), 0, 3);
  A(3) += uint32_t(e16(next16()));
  sub_regs(0, 3, 3);
  movemw(A(3), 3, 3);
  add_regs(3, 0, 3);
  mstore(A(3) + 18, {3, 4, 5});
  asr1(0, 3);
  movemw(A(3) + 6, 3, 5);
  A(4) = uint32_t(e16(rd16(A(3) + 6 + 10)));
  add_regs(3, 0, 3);
  setw(6, lo(6) + lo(0));
  setw(7, lo(7) + lo(1));
  a4_add(2);
  mstore(A(3) + 24, {3, 4, 5, 6, 7, 12});
  A(3) = obj_5b47c();
  setb(1, rd8(A(3) + 124));
  setb(1, lob(1) & 0x7F);
  setb(1, uint32_t(int8_t(lob(1)) >> 4));
  extw(1);
  setw(1, lo(1) + lo(1));
  setw(0, lo(1));
  setw(1, asl16(lo(1), 3));
  setw(1, lo(1) - lo(0));
  A(2) += uint32_t(e16(lo(1)));
  ret(0);
}
void quad_group(uint32_t base, uint32_t src, uint32_t dst, bool six) {
  if (six) {
    movemw(base + src, 3, 5);
    A(4) = uint32_t(e16(rd16(base + src + 10)));
    add_regs(3, 0, 3);
    setw(6, lo(6) + lo(0));
    setw(7, lo(7) + lo(1));
    a4_add(2);
    mstore(base + dst, {3, 4, 5, 6, 7, 12});
  } else {
    movemw(base + src, 3, 3);
    add_regs(3, 0, 3);
    mstore(base + dst, {3, 4, 5});
  }
}
void op_351ac() {
  A(3) = 0x5d684;
  cpu().push32(A(1));
  A(1) = A(3);
  A(1) += uint32_t(e16(next16()));
  setw(7, rd16(A(2)));
  movemw(A(3) + X(7), 0, 3);
  movemw(A(1), 3, 3);
  sub_regs(0, 3, 3);
  quad_group(A(1), 6, 180, true);
  quad_group(A(1), 18, 192, true);
  quad_group(A(1), 30, 204, false);
  setw(7, rd16(A(2)));
  setw(7, lo(7) + 6);
  movemw(A(1), 3, 3);
  movemw(A(3) + X(7), 0, 3);
  sub_regs(0, 3, 3);
  quad_group(A(1), 6, 210, true);
  quad_group(A(1), 18, 222, true);
  quad_group(A(1), 30, 234, false);
  setw(7, next16());
  setw(7, lo(7) + 12);
  movemw(A(1), 3, 3);
  movemw(A(3) + X(7), 0, 3);
  sub_regs(0, 3, 3);
  quad_group(A(1), 6, 240, true);
  quad_group(A(1), 18, 252, true);
  quad_group(A(1), 30, 264, false);
  setw(0, next16());
  A(2) += uint32_t(e16(lo(0)));
  A(1) = cpu().pop32();
  ret(0);
}

void sub_352e2() {
  movemw(A(3) + 18, 0, 8);
  A(4) = uint32_t(e16(rd16(A(3) + 18 + 16)));
  setw(6, lo(6) - lo(0));
  setw(7, lo(7) - lo(1));
  a4_sub(2);
  setw(3, lo(3) + lo(6));
  setw(4, lo(4) + lo(7));
  setw(5, lo(5) + uint16_t(A(4)));
  mstore(A(3) + 84, {3, 4, 5});
  setw(3, lo(3) - lo(6));
  setw(4, lo(4) - lo(7));
  setw(5, lo(5) - uint16_t(A(4)));
  sub_words(0, 3, A(3) + 42);
  add_regs(3, 0, 3);
  mstore(A(3) + 90, {3, 4, 5});
  sub_regs(3, 0, 3);
  setw(2, uint16_t(A(4)));
  setw(6, asr16(lo(6), 2));
  setw(7, asr16(lo(7), 2));
  setw(2, asr16(lo(2), 2));
  A(4) = uint32_t(e16(lo(2)));
  movemw(A(3) + 36, 0, 3);
  sub_words(0, 3, A(3) + 18);
  sub_regs(0, 3, 3);
  negw(0);
  negw(1);
  negw(2);
  mstore(A(3) + 60, {0, 1, 2});
  movemw(A(3) + 18, 3, 3);
  add_words_a3(3, 3, 24);
  asr1(3, 3);
  setw(3, lo(3) + lo(6));
  setw(4, lo(4) + lo(7));
  setw(5, lo(5) + uint16_t(A(4)));
  mstore(A(3) + 66, {3, 4, 5});
  movemw(A(3), 6, 2);
  A(4) = uint32_t(e16(rd16(A(3) + 4)));
  setw(3, lo(3) + lo(6));
  setw(4, lo(4) + lo(7));
  setw(5, lo(5) + uint16_t(A(4)));
  asr1(3, 3);
  mstore(A(3) + 72, {3, 4, 5});
  movemw(A(3) + 42, 0, 3);
  movemw(A(3) + 54, 3, 3);
  sub_regs(3, 0, 3);
  for (int r = 3; r <= 5; ++r) setw(r, asr16(lo(r), 2));
  add_regs(0, 3, 3);
  asr1(3, 3);
  add_regs(0, 3, 3);
  mstore(A(3) + 78, {0, 1, 2});
  ret(0);
}
void op_352c4() {
  A(3) = 0x5b47c;
  setw(0, rd16(0x5acae));
  setw(0, lo(0) + 164);
  A(3) += uint32_t(e16(lo(0)));
  sub_352e2();
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  sub_352e2();
}
void sub_353d8() {
  movemw(A(3), 0, 6);
  sub_regs(3, 0, 3);
  asr1(3, 3);
  add_regs(0, 3, 3);
  mstore(A(3) + 30, {0, 1, 2});
  sub_regs(0, 3, 3);
  asr1(3, 3);
  add_regs(0, 3, 3);
  mstore(A(3) + 36, {0, 1, 2});
  ret(0);
}
void op_353ba() {
  A(3) = 0x5b47c;
  setw(0, rd16(0x5acae));
  setw(0, lo(0) + 164);
  A(3) += uint32_t(e16(lo(0)));
  sub_353d8();
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  sub_353d8();
}

// ---- $35412-$35878: lattice / subdivision ops --------------------------------
void ld6a(uint32_t a) {                               // movem.w a,d3-d7/a4
  movemw(a, 3, 5);
  A(4) = uint32_t(e16(rd16(a + 10)));
}
void ld3a(uint32_t a) {                               // movem.w a,d6-d7/a4
  movemw(a, 6, 2);
  A(4) = uint32_t(e16(rd16(a + 4)));
}
void add_a4w(int r) { setw(r, lo(r) + uint16_t(A(4))); }
void sub_a4w(int r) { setw(r, lo(r) - uint16_t(A(4))); }
void add6_a4(int r) {                                 // d6,d7,a4 += d<r>..d<r+2>
  setw(6, lo(6) + lo(r));
  setw(7, lo(7) + lo(r + 1));
  A(4) += uint32_t(e16(lo(r + 2)));
}
void sub6_a4(int r) {                                 // d6,d7,a4 -= d<r>..
  setw(6, lo(6) - lo(r));
  setw(7, lo(7) - lo(r + 1));
  A(4) -= uint32_t(e16(lo(r + 2)));
}
void add_a4_to(int r) {                               // d<r>..d<r+2> += d6,d7,a4w
  setw(r, lo(r) + lo(6));
  setw(r + 1, lo(r + 1) + lo(7));
  add_a4w(r + 2);
}
void sub_a4_from(int r) {
  setw(r, lo(r) - lo(6));
  setw(r + 1, lo(r + 1) - lo(7));
  sub_a4w(r + 2);
}
void st(uint32_t off, std::initializer_list<int> regs) { mstore(A(3) + off, regs); }

void sub_35462() {
  load9(A(3) + 18);
  sub6_a4(0);
  setw(3, lo(3) + lo(6));
  setw(4, lo(4) + lo(7));
  add_a4w(5);
  st(150, {3, 4, 5});
  setw(3, lo(3) - lo(6));
  setw(4, lo(4) - lo(7));
  sub_a4w(5);
  sub_words(0, 3, A(3) + 48);
  add_regs(3, 0, 3);
  st(102, {3, 4, 5});
  setw(3, lo(3) + lo(6));
  setw(4, lo(4) + lo(7));
  add_a4w(5);
  st(138, {3, 4, 5});
  ld6a(A(3) + 6);
  movemw(A(3) + 36, 0, 3);
  sub_regs(3, 0, 3);
  negw(3);
  negw(4);
  negw(5);
  add_regs(0, 3, 3);
  st(162, {0, 1, 2});
  add6_a4(3);
  st(108, {6, 7, 12});
  add6_a4(3);
  st(168, {6, 7, 12});
  ld3a(A(3) + 54);
  add6_a4(3);
  st(156, {6, 7, 12});
  setw(6, lo(3));
  setw(7, lo(4));
  A(4) = uint32_t(e16(lo(5)));
  asr1(3, 3);
  movemw(A(3) + 42, 0, 3);
  add_regs(0, 3, 3);
  st(114, {0, 1, 2});
  movemw(A(3) + 96, 0, 3);
  add_regs(0, 3, 3);
  st(120, {0, 1, 2});
  setw(6, lo(6) + lo(6));
  setw(7, lo(7) + lo(7));
  A(4) += uint32_t(e16(uint16_t(A(4))));
  add6_a4(3);
  movemw(A(3) + 18, 0, 3);
  add_words_a3(0, 3, 24);
  asr1(0, 3);
  add_a4_to(0);
  st(126, {0, 1, 2});
  ld3a(A(3));
  add_a4_to(0);
  asr1(0, 3);
  st(132, {0, 1, 2});
  add6_a4(3);
  st(174, {6, 7, 12});
  movemw(A(3) + 48, 0, 3);
  add_words_a3(0, 3, 66);
  asr1(0, 3);
  st(144, {0, 1, 2});
}
void op_35412() {
  A(3) = 0x5b47c;
  setw(0, rd16(0x5acae));
  setw(0, lo(0) + 164);
  A(3) += uint32_t(e16(lo(0)));
  sub_35462();
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  sub_35462();
  movemw(A(3) + 78, 0, 6);
  add_regs(3, 0, 3);
  asr1(3, 3);
  movemw(A(3) + 60, 0, 3);
  add_regs(3, 0, 3);
  asr1(3, 3);
  st(600, {3, 4, 5});
  ret(0);
}
void op_35594() {
  A(3) = 0x5d684;
  load9(A(3));
  setw(3, lo(3) - lo(6));
  setw(4, lo(4) - lo(7));
  sub_a4w(5);
  add_regs(0, 3, 3);
  st(66, {0, 1, 2});
  movemw(A(3) + 24, 3, 3);
  sub6_a4(3);
  add_a4_to(0);
  st(72, {0, 1, 2});
  movemw(A(3), 0, 6);
  ld3a(A(3) + 18);
  sub6_a4(3);
  setw(0, lo(0) - lo(6));
  setw(1, lo(1) - lo(7));
  sub_a4w(2);
  st(78, {0, 1, 2});
  add_a4_to(0);
  ld3a(A(3) + 36);
  sub6_a4(3);
  add_a4_to(0);
  st(84, {0, 1, 2});
  movemw(A(3) + 12, 0, 3);
  add_a4_to(0);
  st(90, {0, 1, 2});
  movemw(A(3) + 66, 0, 3);
  add_a4_to(0);
  st(96, {0, 1, 2});
  movemw(A(3) + 54, 0, 3);
  ld6a(A(3) + 18);
  setw(0, lo(0) - lo(6));
  setw(1, lo(1) - lo(7));
  sub_a4w(2);
  add_regs(3, 0, 3);
  st(102, {3, 4, 5});
  ld6a(A(3) + 72);
  add_regs(3, 0, 3);
  add6_a4(0);
  st(108, {3, 4, 5, 6, 7, 12});
  movemw(A(3) + 30, 0, 3);
  movemw(A(3) + 42, 3, 3);
  sub_regs(3, 0, 3);
  sub_regs(0, 3, 3);
  sub_regs(0, 3, 3);
  st(120, {0, 1, 2});
  ret(0);
}
void op_35684() {
  A(3) = 0x5d684;
  load9(A(3) + 6);
  add_regs(3, 0, 3);
  asr1(3, 3);
  copy_regs(0, 3, 3);
  sub6_a4(3);
  sub_regs(3, 6, 2);
  setw(5, lo(5) - uint16_t(A(4)));
  st(30, {0, 1, 2, 3, 4, 5});
  ld3a(A(3));
  sub6_a4(0);
  setw(6, asr16(lo(6), 2));
  setw(7, asr16(lo(7), 2));
  setw(0, lo(0) + lo(6));
  setw(1, lo(1) + lo(7));
  setw(6, uint16_t(A(4)));
  setw(6, asr16(lo(6), 2));
  setw(2, lo(2) + lo(6));
  st(42, {0, 1, 2});
  ld3a(A(3) + 24);
  sub6_a4(3);
  movemw(A(3) + 6, 0, 6);
  add_a4_to(0);
  add_a4_to(3);
  st(48, {0, 1, 2, 3, 4, 5});
  movemw(A(3) + 18, 0, 3);
  add_a4_to(0);
  st(60, {0, 1, 2});
  movemw(A(3) + 42, 0, 3);
  movemw(A(3), 3, 3);
  sub_regs(3, 0, 3);
  sub_regs(0, 3, 3);
  sub_regs(0, 3, 3);
  st(66, {0, 1, 2});
  ret(0);
}
void sub_3574c() {
  static const uint32_t offs[] = {0, 18, 42, 60, 78};
  static const uint32_t dst[] = {96, 102, 108, 114, 120};
  for (int i = 0; i < 5; ++i) {
    load9(A(3) + offs[i]);
    setw(6, lo(6) - lo(3));
    setw(7, lo(7) - lo(4));
    A(4) -= uint32_t(e16(lo(5)));
    add_a4_to(0);
    st(dst[i], {0, 1, 2});
  }
  ret(0);
}
void op_35730() {
  A(3) = 0x5b47c;
  setw(0, rd16(0x5acae));
  setw(0, lo(0) + 164);
  A(3) += uint32_t(e16(lo(0)));
  sub_3574c();
  A(3) = 0x5d684;
  sub_3574c();
}

// $35832: line from vertex a3 (scaled by fp-8), length class -> $438a8
void sub_35832() {
  movemw(A(3), 3, 3);
  setw(0, RW(-8));
  for (int r = 3; r <= 5; ++r) setw(r, asl16(lo(r), D(0)));
  cpu().a[7] -= 8;
  mstore(cpu().a[7], {3, 4, 5, 6});
  setw(0, lo(3));
  if (int16_t(lo(0)) < 0) negw(0);
  setw(1, lo(4));
  if (int16_t(lo(1)) < 0) negw(1);
  setw(2, lo(5));
  if (int16_t(lo(2)) < 0) negw(2);
  setw(4, lo(2));
  setw(3, lo(1));
  setw(2, lo(0));
  call_c(0x308cc);
  setw(7, lo(1));
  movemw(cpu().a[7], 0, 3);
  D(6) = uint32_t(e16(rd16(cpu().a[7] + 6)));
  cpu().a[7] += 8;
  if (int16_t(lo(7)) <= 0) {
    setw(6, 127);
  } else {
    divuw(D(6), lo(7));
    if (!(int16_t(lo(6)) <= 127)) setw(6, 127);
  }
  call_c(0x438a8);
}
NATIVE_ASM(0x35832, sub_35832, true);

void op_357d0() {
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  wr16(0x5ac4c, next16());
  setw(6, next16());
  push_a1_a2_a5();
  sub_35832();
  pop_a1_a2_a5();
}
void op_357ee() {
  push_a1_a5();
  WW(-126, 0);
  WW(-110, 0);
  while (RW(-110) == 0) {                             // $357fa
    A(3) = 0x5d684 + uint32_t(e16(next16()));
    wr16(0x5ac4c, next16());
    setw(6, next16());
    if (int16_t(lo(6)) < 0) {
      WW(-110, RW(-110) + 1);
      setw(6, lo(6) & 0x7FFF);
    }
    cpu().push32(A(2));
    sub_35832();
    or_result();
    A(2) = cpu().pop32();
  }
  pop_a1_a5();
  ret_fp126();
}

// ---- $35b6c-$35f48: more lattice ops (object-relative and table-relative) -------
void ld_d012_d67_a4(uint32_t a) {                     // movem.w a,d0-d2/d6-d7/a4
  movemw(a, 0, 3);
  D(6) = uint32_t(e16(rd16(a + 6)));
  D(7) = uint32_t(e16(rd16(a + 8)));
  A(4) = uint32_t(e16(rd16(a + 10)));
}
void obj_a3_plus_164() {
  A(3) = 0x5b47c;
  setw(0, rd16(0x5acae));
  setw(0, lo(0) + 164);
  A(3) += uint32_t(e16(lo(0)));
}
void sub_35bbc() {
  movemw(A(3) + 18, 0, 6);
  add_regs(3, 0, 3);
  asr1(3, 3);
  st(132, {3, 4, 5});
  movemw(A(3) + 54, 0, 3);
  setw(6, lo(0));
  setw(7, lo(1));
  A(4) = uint32_t(e16(lo(2)));
  sub_regs(0, 3, 3);
  sub_regs(3, 0, 3);
  st(138, {3, 4, 5});
  movemw(A(3) + 48, 0, 3);
  sub6_a4(0);
  add6_a4(3);
  st(144, {6, 7, 12});
  load9(A(3) + 18);
  sub6_a4(0);
  setw(3, lo(3) + lo(6));
  setw(4, lo(4) + lo(7));
  add_a4w(5);
  st(150, {3, 4, 5});
  movemw(A(3) + 138, 0, 6);
  add_a4_to(0);
  add_a4_to(3);
  st(228, {0, 1, 2, 3, 4, 5});
  add_regs(3, 0, 3);
  asr1(3, 3);
  st(168, {3, 4, 5});
  movemw(A(3) + 30, 0, 3);
  movemw(A(3) + 150, 3, 3);
  setw(6, lo(3));
  setw(7, lo(4));
  A(4) = uint32_t(e16(lo(5)));
  add_regs(3, 0, 3);
  asr1(3, 3);
  add_regs(0, 3, 3);
  asr1(0, 3);
  setw(3, lo(3) + lo(6));
  setw(4, lo(4) + lo(7));
  add_a4w(5);
  asr1(3, 3);
  st(156, {0, 1, 2, 3, 4, 5});
  movemw(A(3) + 6, 0, 6);
  add_regs(3, 0, 3);
  asr1(3, 3);
  st(174, {3, 4, 5});
  movemw(A(3) + 36, 0, 6);
  ld3a(A(3) + 60);
  sub_regs(0, 3, 3);
  add6_a4(0);
  st(126, {6, 7, 12});
  movemw(A(3) + 72, 3, 3);
  add_regs(3, 0, 3);
  ld3a(A(3) + 84);
  add6_a4(0);
  st(180, {3, 4, 5, 6, 7, 12});
  copy_regs(3, 0, 3);
  asr1(0, 3);
  add_regs(3, 0, 3);
  ld_d012_d67_a4(A(3) + 6);
  add_regs(0, 3, 3);
  add6_a4(3);
  st(192, {0, 1, 2, 6, 7, 12});
  ld_d012_d67_a4(A(3) + 36);
  setw(0, lo(0) - lo(6));
  setw(1, lo(1) - lo(7));
  sub_a4w(2);
  ld3a(A(3) + 192);
  add6_a4(0);
  st(216, {6, 7, 12});
  ld3a(A(3) + 198);
  add6_a4(0);
  st(222, {6, 7, 12});
  add_regs(3, 0, 3);
  movemw(A(3) + 66, 0, 3);
  add_regs(0, 3, 3);
  ld3a(A(3) + 78);
  add6_a4(3);
  st(204, {0, 1, 2, 6, 7, 12});
}
void op_35b6c() {
  obj_a3_plus_164();
  sub_35bbc();
  A(3) = 0x5d684 + uint32_t(e16(next16()));
  sub_35bbc();
  movemw(A(3) + 108, 0, 6);
  add_regs(3, 0, 3);
  asr1(3, 3);
  movemw(A(3) + 96, 0, 3);
  add_regs(3, 0, 3);
  asr1(3, 3);
  st(660, {3, 4, 5});
  ret(0);
}
// one 'quad' block of $35d78: d0-d2 = base point, a3+src = three more points
void block_35d78(uint32_t src, uint32_t dst1, uint32_t dst2, bool restore) {
  ld6a(A(3) + src);
  sub_regs(3, 0, 3);
  sub_regs(0, 3, 3);
  st(dst1, {0, 1, 2});
  add_regs(0, 3, 3);
  sub6_a4(0);
  sub_a4_from(0);
  st(dst2, {0, 1, 2});
  if (restore) add_a4_to(0);
}
void sub_35d78() {
  movemw(A(3) + 24, 0, 3);
  block_35d78(42, 90, 96, true);
  block_35d78(54, 102, 108, false);
  movemw(A(3) + 18, 0, 3);
  block_35d78(66, 114, 120, true);
  block_35d78(78, 126, 132, false);
  ret(0);
}
void op_35d5c() {
  obj_a3_plus_164();
  sub_35d78();
  A(3) = 0x5d684;
  sub_35d78();
}
void sub_35e70() {
  load9(A(3));
  sub6_a4(0);
  setw(3, lo(3) + lo(6));
  setw(4, lo(4) + lo(7));
  add_a4w(5);
  st(138, {3, 4, 5});
  add_regs(3, 0, 3);
  asr1(3, 3);
  st(144, {3, 4, 5});
  sub_regs(3, 0, 3);
  asr1(3, 3);
  sub_regs(0, 3, 3);
  st(150, {0, 1, 2});
  ld3a(A(3) + 6);
  movemw(A(3) + 144, 0, 3);
  setw(0, lo(0) - lo(6));
  setw(1, lo(1) - lo(7));
  sub_a4w(2);
  asr1(0, 3);
  sub6_a4(0);
  st(162, {6, 7, 12});
  sub6_a4(3);
  st(168, {6, 7, 12});
  sub_words(0, 3, A(3) + 150);
  negw(0);
  negw(1);
  negw(2);
  st(156, {0, 1, 2});
  ret(0);
}
void op_35e54() {
  obj_a3_plus_164();
  sub_35e70();
  A(3) = 0x5d684;
  sub_35e70();
}
void sub_35f18() {
  load9(A(3));
  sub6_a4(0);
  setw(3, lo(3) + lo(6));
  setw(4, lo(4) + lo(7));
  add_a4w(5);
  st(138, {3, 4, 5});
  movemw(A(3) + 30, 3, 3);
  setw(3, lo(3) + lo(6));
  setw(4, lo(4) + lo(7));
  add_a4w(5);
  st(144, {3, 4, 5});
  ret(0);
}
void op_35efc() {
  obj_a3_plus_164();
  sub_35f18();
  A(3) = 0x5d684;
  sub_35f18();
}
void op_35f44() { ret(0); }
void op_35f48() { ret(0); }

// ---- $35884: draw the four-point tail/flare light set of an object ---------------
// Own `link a6,#-46` frame.  Locals: -10 b count, -30 w result, -32 b index,
// -34 w direction, -38 w, -44..-36 saved words.
uint8_t RB(int32_t off) { return rd8(FP() + uint32_t(off)); }
void WB(int32_t off, uint32_t v) { wr8(FP() + uint32_t(off), v); }
void light_step(int shift, bool second, bool reload) {  // $35a5e.. one interpolated point
  if (reload) {
    movemw(cpu().a[7], 0, 3);
    D(6) = uint32_t(e16(rd16(cpu().a[7] + 6)));
    A(3) = uint32_t(e16(rd16(cpu().a[7] + 8)));
  }
  movemw(FP() - 44, 3, 3);
  D(7) = uint32_t(e16(rd16(FP() - 44 + 6)));
  A(4) = uint32_t(e16(rd16(FP() - 44 + 8)));
  swapw(0);
  A(4) -= uint32_t(e16(uint16_t(A(3))));
  setw(0, uint16_t(A(4)));
  for (int r = 3; r <= 5; ++r) setw(r, asl16(lo(r), D(0)));
  swapw(0);
  if (!second) {
    sub_regs(0, 3, 3);
    setw(6, lo(6) - lo(7));
    for (int r : {0, 1, 2, 6}) setw(r, asr16(lo(r), shift));
    add_regs(0, 3, 3);
    setw(6, lo(6) + lo(7));
  } else {
    sub_regs(3, 0, 3);
    setw(7, lo(7) - lo(6));
    for (int r : {3, 4, 5, 7}) setw(r, asr16(lo(r), shift));
    add_regs(0, 3, 3);
    setw(6, lo(6) + lo(7));
  }
  call_c(0x438a8);
  WW(-30, RW(-30) | lo(0));
}
void op_35884() {
  setw(0, rd16(0x5acae));
  if (lo(0) != rd16(0x650c2)) return ret(0);
  cpu().push32(cpu().a[6]);                            // link a6,#-46
  cpu().a[6] = cpu().a[7];
  cpu().a[7] -= 46;
  WW(-38, 0xFFFF);
  WW(-30, 0);
  A(1) = 0x5b47c + uint32_t(e16(lo(0)));
  setb(0, 0);
  if (int8_t(rd8(0x650c0)) >= 6) setb(0, rd8(0x650c1));
  WB(-32, lob(0));
  WB(-10, rd8(A(1) + 61));
  if (int8_t(RB(-10)) <= 0) goto L35b64;
  if (!(int8_t(rd8(0x650c0)) >= 6)) WB(-10, RB(-10) - 1);
  D(0) = rd32(A(1) + 62);
  D(1) = rd32(A(1) + 66);
  D(2) = rd32(A(1) + 70);
  D(3) = rd32(A(1) + 20);
  D(4) = rd32(A(1) + 24);
  D(5) = rd32(A(1) + 28);
  D(3) -= rd32(0x5ad74);
  D(4) -= rd32(0x5ad78);
  D(5) -= rd32(0x5ad7c);
  if (!(int16_t(rd16(A(1) + 110)) < 256)) {
    for (int r = 0; r <= 2; ++r) D(r) = uint32_t(asr32(int32_t(D(r)), 8));
  }
  for (int r = 3; r <= 5; ++r) D(r) = uint32_t(asr32(int32_t(D(r)), 8));
  D(0) = uint32_t(int32_t(int16_t(lo(0))) * int16_t(lo(3)));
  D(1) = uint32_t(int32_t(int16_t(lo(1))) * int16_t(lo(4)));
  D(2) = uint32_t(int32_t(int16_t(lo(2))) * int16_t(lo(5)));
  {
    int64_t sum = int64_t(int32_t(D(2))) + int32_t(D(0));
    D(2) += D(0);
    sum = int64_t(int32_t(D(2))) + int32_t(D(1));
    D(2) += D(1);
    if (sum < 0) {
      WW(-34, 0xFFFF);
    } else {
      WW(-34, 0);
      setb(0, rd8(0x650c1));
      setb(0, lob(0) - rd8(0x650c0));
      if (!(int8_t(rd8(0x650c0)) >= 6)) setb(0, lob(0) - 1);
      int r = int8_t(lob(0)) + int8_t(rd8(A(1) + 61));   // addb; bges: true sum >= 0
      setb(0, uint32_t(r));
      if (r < 0) setb(0, lob(0) + rd8(0x650c0));
      WB(-32, lob(0));
    }
  }
L35948:
  A(1) = 0x5b47c + uint32_t(e16(rd16(0x5acae)));
  setb(0, RB(-32));
  extw(0);
  setw(0, lo(0) + lo(0));
  setw(0, lo(0) + lo(0));
  setw(3, lo(0));
  setw(0, lo(0) + lo(0));
  setw(0, lo(0) + lo(3));
  A(3) = 0x650c4 + uint32_t(e16(lo(0)));
  D(3) = rd32(A(3));
  D(4) = rd32(A(3) + 4);
  D(5) = rd32(A(3) + 8);
  D(3) -= rd32(0x5ad74);
  D(4) -= rd32(0x5ad78);
  D(5) -= rd32(0x5ad7c);
  D(7) = 0;
  D(0) = D(3);
  if (int32_t(D(0)) < 0) negl(0);
  D(1) = D(4);
  if (int32_t(D(1)) < 0) negl(1);
  D(2) = D(5);
  if (int32_t(D(2)) < 0) negl(2);
  D(6) = 16384;
  if (int32_t(D(0)) > int32_t(D(6)) || int32_t(D(1)) > int32_t(D(6)) ||
      int32_t(D(2)) > int32_t(D(6))) {
    D(7) = 8;
    D(6) = 262144;
    if (!(int32_t(D(0)) > int32_t(D(6)) || int32_t(D(1)) > int32_t(D(6)) ||
          int32_t(D(2)) > int32_t(D(6))))
      D(7) = 4;
    for (int r : {3, 4, 5, 0, 1, 2}) D(r) = uint32_t(asr32(int32_t(D(r)), D(7)));
  }
  if (RB(-32) & 1) wr16(0x5ac4c, (rd8(A(1) + 32) & 2) ? 3 : 8);
  else wr16(0x5ac4c, (rd8(A(1) + 32) & 2) ? 12 : 10);
  setw(6, 2496);                                       // $35a08
  cpu().a[7] -= 8;
  mstore(cpu().a[7], {3, 4, 5, 6});
  setw(4, lo(2));
  setw(3, lo(1));
  setw(2, lo(0));
  call_c(0x308cc);
  movemw(cpu().a[7], 3, 4);
  cpu().a[7] += 8;
  D(0) = 8;
  setw(0, lo(0) - lo(7));
  A(3) = uint32_t(e16(lo(7)));
  setw(1, asr16(lo(1), D(0)));
  setw(7, lo(1));
  cpu().a[7] -= 2;
  wr16(cpu().a[7], lo(6));
  call_c(0x41666);
  setw(0, lo(6));
  setw(1, lo(2));
  setw(2, lo(5));
  setw(6, rd16(cpu().a[7]));
  cpu().a[7] += 2;
  extl(6);
  if (int16_t(lo(7)) <= 0) {
    setw(6, 127);
  } else {
    divuw(D(6), lo(7));
    if (!(int16_t(lo(6)) <= 127)) setw(6, 127);
  }
  if (!(int16_t(RW(-38)) < 0) && int8_t(rd8(A(1) + 61)) > 1) {
    cpu().a[7] -= 10;                                  // movem.w d0-d2/d6/a3,-(sp)
    mstore(cpu().a[7], {0, 1, 2, 6, 11});
    light_step(2, false, false);
    light_step(1, false, true);
    light_step(2, true, true);
    movemw(cpu().a[7], 0, 3);
    D(6) = uint32_t(e16(rd16(cpu().a[7] + 6)));
    A(3) = uint32_t(e16(rd16(cpu().a[7] + 8)));
    cpu().a[7] += 10;
  }
  mstore(FP() - 44, {0, 1, 2, 6, 11});                 // $35b10
  if (!(int8_t(RB(-10)) > 0)) {
    setw(7, lo(6));
    setw(7, asr16(lo(7), 3));
    setw(6, lo(6) - lo(7));
  }
  call_c(0x438a8);
  WW(-30, RW(-30) | lo(0));
  if (!(int16_t(RW(-34)) < 0)) {
    WB(-32, RB(-32) - 1);
    if (!(int8_t(RB(-32)) >= 0)) {
      setb(0, rd8(0x650c0));
      setb(0, lob(0) - 1);
      WB(-32, lob(0));
    }
  } else {
    WB(-32, RB(-32) + 1);
    setb(0, rd8(0x650c0));
    setb(0, lob(0) - 1);
    if (!(int8_t(lob(0)) >= int8_t(RB(-32)))) WB(-32, 0);
  }
  WB(-10, RB(-10) - 1);
  if (int8_t(RB(-10)) >= 0) goto L35948;
L35b64:
  setw(0, RW(-30));
  cpu().a[7] = cpu().a[6];                             // unlk a6
  cpu().a[6] = cpu().pop32();
  flags(lo(0), 16);
  return;
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
NATIVE_ASM(0x341d0, op_341d0, true);
NATIVE_ASM(0x341e2, op_341e2, true);
NATIVE_ASM(0x343b2, op_343b2, true);
NATIVE_ASM(0x343c8, op_343c8, true);
NATIVE_ASM(0x344f8, op_344f8, true);
NATIVE_ASM(0x345d0, op_345d0, true);
NATIVE_ASM(0x345de, op_345de, true);
NATIVE_ASM(0x34654, op_34654, true);
NATIVE_ASM(0x346a0, op_346a0, true);
NATIVE_ASM(0x34708, op_34708, true);
NATIVE_ASM(0x34754, op_34754, true);
NATIVE_ASM(0x347ec, op_347ec, true);
NATIVE_ASM(0x34872, op_34872, true);
NATIVE_ASM(0x34968, op_34968, true);
NATIVE_ASM(0x349b6, op_349b6, true);
NATIVE_ASM(0x34a28, op_34a28, true);
NATIVE_ASM(0x34a3c, op_34a3c, true);
NATIVE_ASM(0x34aa8, op_34aa8, true);
NATIVE_ASM(0x34b2a, op_34b2a, true);
NATIVE_ASM(0x34b9e, op_34b9e, true);
NATIVE_ASM(0x34c1c, op_34c1c, true);
NATIVE_ASM(0x34c8c, op_34c8c, true);
NATIVE_ASM(0x34d2a, op_34d2a, true);
NATIVE_ASM(0x34e16, op_34e16, true);
NATIVE_ASM(0x34f76, op_34f76, true);
NATIVE_ASM(0x35054, op_35054, true);
NATIVE_ASM(0x350a2, op_350a2, true);
NATIVE_ASM(0x350cc, op_350cc, true);
NATIVE_ASM(0x350ec, op_350ec, true);
NATIVE_ASM(0x350f2, op_350f2, true);
NATIVE_ASM(0x35118, op_35118, true);
NATIVE_ASM(0x3513a, op_3513a, true);
NATIVE_ASM(0x351ac, op_351ac, true);
NATIVE_ASM(0x352c4, op_352c4, true);
NATIVE_ASM(0x353ba, op_353ba, true);
NATIVE_ASM(0x35412, op_35412, true);
NATIVE_ASM(0x35594, op_35594, true);
NATIVE_ASM(0x35684, op_35684, true);
NATIVE_ASM(0x35730, op_35730, true);
NATIVE_ASM(0x357d0, op_357d0, true);
NATIVE_ASM(0x357ee, op_357ee, true);
NATIVE_ASM(0x35b6c, op_35b6c, true);
NATIVE_ASM(0x35d5c, op_35d5c, true);
NATIVE_ASM(0x35e54, op_35e54, true);
NATIVE_ASM(0x35efc, op_35efc, true);
NATIVE_ASM(0x35f44, op_35f44, true);
NATIVE_ASM(0x35f48, op_35f48, true);
NATIVE_ASM(0x35884, op_35884, true);

// Internal lattice helpers used by the ops above — register so original
// callers that jump here directly take the native path.
NATIVE_ASM(0x352e2, sub_352e2, true);
NATIVE_ASM(0x353d8, sub_353d8, true);
NATIVE_ASM(0x35462, sub_35462, true);
NATIVE_ASM(0x3574c, sub_3574c, true);
NATIVE_ASM(0x35bbc, sub_35bbc, true);
NATIVE_ASM(0x35d78, sub_35d78, true);
NATIVE_ASM(0x35e70, sub_35e70, true);
NATIVE_ASM(0x35f18, sub_35f18, true);

}  // namespace native
