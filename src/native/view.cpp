// View setup of the 3D scene ($4217c) and the angle -> matrix builders it
// uses.  Angles are in 1/80 degree (28800 = 360 degrees); the builders take
// them >> 3 (3600 = 360) and look up the sine table $538e8 (Q14, 16384 =
// 1.0, indexed by the doubled angle in 0..90 degrees).  Matrices are 3x3
// words, rows of 1/256 fixed point ($42b50: Q14).  See
// docs/ground_sea_sky.md, "View transform".
// Register-convention assembly on the live register file.
#include "native/asmregs.hpp"
#include "native/orig.hpp"
#include "native/vars.hpp"

namespace native {
using namespace asmr;

namespace {

constexpr uint32_t sine_table = 0x538e8;

void put(uint16_t v) {                    // movew ...,(a1)+
  wr16(A(1), v);
  A(1) += 2;
}
void negw(int r) { setw(r, uint16_t(-int16_t(lo(r)))); }
void lsrw3(int r) { setw(r, uint16_t(lo(r) >> 3)); }
// movew dA,d6; mulsw dB,d6 (32-bit product)
void mul6(int a, int b) {
  setw(6, lo(a));
  D(6) = uint32_t(int32_t(int16_t(lo(6))) * int16_t(lo(b)));
}
void mul7(int a, int b) {
  setw(7, lo(a));
  D(7) = uint32_t(int32_t(int16_t(lo(7))) * int16_t(lo(b)));
}
// swap dN; asrw #4,dN: the high word of a Q14*Q14 product, as 1/256
void hi_asr4(int n) {
  swapw(n);
  setw(n, asr16(lo(n), 4));
}
void asr14(int n) { D(n) = uint32_t(int32_t(D(n)) >> 14); }  // moveq #14,dX; asrl dX,dN

// ---- $42dd6: sine/cosine of the angles in d0 and d2 ------------------------------
// d0/d1 = sin/cos of d0, d2/d3 = sin/cos of d2 (Q14).
void quadrant(int a, int c) {
  A(0) = sine_table;
  setw(7, lo(a));
  auto at = [](int r) { return rd16(A(0) + uint32_t(ext(lo(r)))); };
  if (int16_t(lo(a)) < 1800) {
    setw(a, at(a));
    setw(6, 1800);
    setw(6, lo(6) - lo(7));
    setw(c, at(6));
  } else if (int16_t(lo(a)) < 3600) {
    setw(6, 3600);
    setw(6, lo(6) - lo(a));
    setw(a, at(6));
    setw(6, 1800);
    setw(7, lo(7) - lo(6));
    setw(c, at(7));
    negw(c);
  } else if (int16_t(lo(a)) < 5400) {
    setw(6, 3600);
    setw(a, lo(a) - lo(6));
    setw(a, at(a));
    negw(a);
    setw(6, 5400);
    setw(6, lo(6) - lo(7));
    setw(c, at(6));
    negw(c);
  } else {
    setw(6, 7200);
    setw(6, lo(6) - lo(a));
    setw(a, at(6));
    negw(a);
    setw(6, 5400);
    setw(7, lo(7) - lo(6));
    setw(c, at(7));
  }
}
void sincos_42dd6() {
  setw(0, lo(0) + lo(0));
  setw(2, lo(2) + lo(2));
  quadrant(0, 1);
  quadrant(2, 3);
}

// ---- $42b26: rotation about the vertical axis by d4 (pan), 1/256 ------------------
void pan_42b26() {
  setw(4, asr16(lo(4), 3));
  call_c(0x42eba);                         // d4/d5 = sin/cos
  setw(6, asr16(lo(5), 6));
  put(lo(6));
  put(0);
  setw(6, asr16(lo(4), 6));
  put(lo(6));
  put(0);
  put(256);
  put(0);
  setw(4, asr16(lo(4), 6));
  negw(4);
  put(lo(4));
  put(0);
  setw(5, asr16(lo(5), 6));
  put(lo(5));
}

// ---- $42b50: the same in Q14 -------------------------------------------------------
void pan_42b50() {
  setw(4, asr16(lo(4), 3));
  call_c(0x42eba);
  put(lo(5));
  put(0);
  put(lo(4));
  put(0);
  put(16384);
  put(0);
  negw(4);
  put(lo(4));
  put(0);
  put(lo(5));
}

// ---- $42b6e: view matrix from pitch d0 and heading d2 ------------------------------
void two_angles_42b6e() {
  lsrw3(0);
  lsrw3(2);
  sincos_42dd6();
  setw(6, asr16(lo(3), 6));
  put(lo(6));
  put(0);
  setw(6, asr16(lo(2), 6));
  put(lo(6));
  mul6(2, 0);
  hi_asr4(6);
  negw(6);
  put(lo(6));
  setw(6, asr16(lo(1), 6));
  put(lo(6));
  mul6(3, 0);
  hi_asr4(6);
  put(lo(6));
  mul6(2, 1);
  hi_asr4(6);
  negw(6);
  put(lo(6));
  negw(0);
  setw(0, asr16(lo(0), 6));
  put(lo(0));
  mul6(3, 1);
  hi_asr4(6);
  wr16(A(1), lo(6));
}

// ---- $42bbe: matrix from three angles d0, d2, d4 -----------------------------------
void three_angles_42bbe() {
  lsrw3(0);
  lsrw3(2);
  lsrw3(4);
  sincos_42dd6();
  call_c(0x42eba);
  mul6(2, 0);                              // row 0
  D(7) = 14;
  asr14(6);
  setw(6, lo(6));
  D(6) = uint32_t(int32_t(int16_t(lo(6))) * int16_t(lo(4)));
  mul7(3, 5);
  D(7) += D(6);
  hi_asr4(7);
  put(lo(7));
  mul6(1, 4);
  hi_asr4(6);
  negw(6);
  put(lo(6));
  mul6(3, 0);
  swapw(7);
  D(7) = 14;
  asr14(6);
  D(6) = uint32_t(int32_t(int16_t(lo(6))) * int16_t(lo(4)));
  mul7(2, 5);
  D(7) -= D(6);
  hi_asr4(7);
  put(lo(7));
  mul6(2, 0);                              // row 1
  D(7) = 14;
  asr14(6);
  D(6) = uint32_t(int32_t(int16_t(lo(6))) * int16_t(lo(5)));
  mul7(3, 4);
  D(7) -= D(6);
  hi_asr4(7);
  put(lo(7));
  mul6(1, 5);
  hi_asr4(6);
  put(lo(6));
  mul6(3, 0);
  D(7) = 14;
  asr14(6);
  D(6) = uint32_t(int32_t(int16_t(lo(6))) * int16_t(lo(5)));
  mul7(2, 4);
  D(7) += D(6);
  hi_asr4(7);
  put(lo(7));
  mul6(2, 1);                              // row 2
  hi_asr4(6);
  negw(6);
  put(lo(6));
  setw(0, asr16(lo(0), 6));
  negw(0);
  put(lo(0));
  mul6(3, 1);
  hi_asr4(6);
  wr16(A(1), lo(6));
}

// ---- $4217c ------------------------------------------------------------------------
void finish_42290() {                       // view matrix from $5ad8c/$5ad8e
  setw(0, rd16(0x5ad8c));
  setw(2, rd16(0x5ad8e));
  A(1) = 0x5aed0;
  two_angles_42b6e();
  A(1) = 0x5aed0;
  call_c(0x42d8c);                          // row scales
  setw(4, rd16(0x5ad8e));
  A(1) = 0x5aef4;
  pan_42b26();
  for (int i = 0; i < 3; ++i) D(i) = uint32_t(ext(rd16(0x5b4e2 + 2u * uint32_t(i))));
  for (int i = 0; i < 3; ++i) wr32(0x5ad80 + 4u * uint32_t(i), D(i));
}

void external_4219a() {
  m68k::Cpu& c = cpu();
  if (rd8(0x5aaac) == 0) {                  // $42190
    call_c(0x39e34);
    return finish_42290();
  }
  A(1) = 0x5b47c;                           // subject record
  setw(2, rd16(0x5abd6));
  bool add = lo(2) != 0;
  if (!add) {
    setw(2, rd16(0x5acba));
    add = (rd8(A(1) + 1 + uint32_t(ext(lo(2)))) & 0x40) &&
          rd8(A(1) + 98 + uint32_t(ext(lo(2)))) == 48;  // a1@($62,d2.w)
  }
  if (add) A(1) += uint32_t(ext(lo(2)));
  setb(0, rd8(A(1) + 98));
  setb(0, lob(0) & 0xF0);
  D(3) = 0;                                 // seat offset (d3, d4, d5)
  D(4) = 0;
  if (lob(0) == 48) {
    D(5) = uint32_t(-4);
  } else {
    D(5) = 1;
    if (rd8(0x5aba6) == 6) D(5) = 5;
  }
  call_c(0x2d4a0);                          // eye = record + matrix * offset
  D(4) = D(2);
  D(3) = D(1);
  D(2) = D(0);
  D(2) -= rd32(0x5af36);
  D(3) -= rd32(0x5af3a);
  D(4) -= rd32(0x5af3e);
  if (int8_t(rd8(0x5aaac)) < 0) {
    D(5) = uint32_t(-1);
  } else if (rd8(0x5aba6) != 0) {
    D(5) = 560;
  } else {
    D(5) = 0;
    setw(5, rd16(0x5abd6));
    setb(5, lob(5) | rd8(0x5abaa));
    if (lo(5) == rd16(0x5ad58)) {
      D(5) = 2000;
    } else {
      wr16(0x5ad58, lo(5));
      D(5) = uint32_t(-1);
    }
  }
  if (rd8(0x5aaa6) == 0) {                  // look angles towards the subject
    setw(0, rd16(0x5ad8c));
    setw(1, rd16(0x5ad8e));
    extl(0);
    extl(1);
    if (rd8(0x5aaad) == 0) D(5) = uint32_t(-1);
    c.a[7] -= 24;                           // moveml d0-d5,-(sp)
    for (int i = 0; i < 6; ++i) wr32(c.a[7] + 4u * uint32_t(i), D(i));
    call_c(0x24552);
    c.a[7] += 24;
    wr16(0x5ad8c, rd16(0x5adb8));
    wr16(0x5ad8e, rd16(0x5adba));
  }
  finish_42290();
}

void cockpit_422f8() {
  m68k::Cpu& c = cpu();
  A(0) = 0x5b47c + uint32_t(ext(rd16(0x5abd6)));
  setw(4, rd16(A(0) + 104));
  A(1) = 0x5aef4;
  pan_42b26();
  A(0) = 0x5b47c + uint32_t(ext(rd16(0x5abd6)));
  if (rd8(0x5aa7d) == 0) {                  // attitude matrix $5aee2
    D(0) = uint32_t(ext(rd16(A(0) + 102)));
    D(2) = uint32_t(ext(rd16(A(0) + 104)));
    D(4) = uint32_t(ext(rd16(A(0) + 106)));
    A(1) = 0x5aee2;
    uint32_t keep = A(0);
    three_angles_42bbe();
    A(0) = keep;
  }
  D(0) = 0;
  D(2) = 0;
  D(4) = 0;
  setb(3, rd8(A(0) + 98));
  setb(3, lob(3) & 0xF0);
  setb(1, rd8(0x5aa9f));
  enum { Table, Angles, Player } next;
  if (lob(1) != 0) {                        // look direction keys
    if (int8_t(lob(1)) <= 11) {
      next = Table;
    } else {
      setb(1, lob(1) - 11);
      next = Angles;
      if (lob(3) != 48) {                   // $4239a
        setb(1, lob(1) - 1);
        bool fwd = lob(1) == 0;
        setw(0, rd16(A(0) + 102));
        bool out = int16_t(lo(0)) < 560 || int16_t(lo(0)) >= 28240;
        if (fwd) setw(0, out ? 22160 : 21600);
        else setw(0, out ? 6640 : 7200);
      } else {                              // $423da
        setb(1, lob(1) - 1);
        bool fwd = lob(1) == 0;
        setw(0, rd16(A(0) + 102));
        bool out = int16_t(lo(0)) >= 7760 || int16_t(lo(0)) <= 6640;
        if (fwd) setw(0, out ? 14400 : 13840);
        else setw(0, out ? 0 : 560);
      }
    }
  } else if (lob(3) == 48) {
    D(1) = 12;
    next = Table;
  } else {
    setb(1, rd8(0x5aaa9));
    if (int8_t(lob(1)) <= 1) {
      next = Player;
    } else {
      setb(1, lob(1) - 2);
      next = Angles;
      if (lob(1) == 0) setw(2, 28160);      // $4241c
      else setw(2, 640);                    // $42416
    }
  }
  if (next == Table) {                      // $42422: preset view angles
    A(1) = lob(3) == 48 ? 0x424e4 : 0x424a2;
    setw(1, lo(1) - 1);
    extw(1);
    setw(1, lo(1) + lo(1));
    setw(0, lo(1));
    setw(1, lo(1) + lo(1));
    setw(1, lo(1) + lo(0));
    A(1) += uint32_t(ext(lo(1)));
    setw(0, rd16(A(1)));
    setw(2, rd16(A(1) + 2));
    setw(4, rd16(A(1) + 4));
    A(1) += 4;
    next = Angles;
  }
  if (next == Player) {
    D(0) = uint32_t(ext(rd16(A(0) + 102)));
    D(2) = uint32_t(ext(rd16(A(0) + 104)));
    D(4) = uint32_t(ext(rd16(A(0) + 106)));
    wr32(0x5ad80, D(0));
    wr32(0x5ad84, D(2));
    wr32(0x5ad88, D(4));
  } else {                                  // $4244a: view direction offset
    c.push32(A(0));
    c.a[7] -= 6;
    wr16(c.a[7], lo(0));
    wr16(c.a[7] + 2, lo(2));
    wr16(c.a[7] + 4, lo(4));
    A(1) = 0x5af18;
    three_angles_42bbe();
    A(1) = 0x5af18;
    call_c(0x42d8c);
    D(0) = uint32_t(ext(rd16(c.a[7])));
    D(2) = uint32_t(ext(rd16(c.a[7] + 2)));
    D(4) = uint32_t(ext(rd16(c.a[7] + 4)));
    c.a[7] += 6;
    A(0) = c.pop32();
    A(4) = A(0) + 128;
    call_c(0x426c0);                        // combine with the player's angles
    wr32(0x5ad80, D(4));
    wr32(0x5ad84, D(5));
    wr32(0x5ad88, D(6));
  }
  setw(0, rd16(0x5ad82));                   // $4247a: view matrix
  setw(2, rd16(0x5ad86));
  setw(4, rd16(0x5ad8a));
  A(1) = 0x5aed0;
  three_angles_42bbe();
  A(1) = 0x5aed0;
  call_c(0x42d8c);
}

void view_4217c() {
  if (rd8(0x5aa7d) != 0) external_4219a();
  else cockpit_422f8();
}

}  // namespace

NATIVE_ASM(0x42dd6, sincos_42dd6, true);
NATIVE_ASM(0x42b26, pan_42b26, true);
NATIVE_ASM(0x42b50, pan_42b50, true);
NATIVE_ASM(0x42b6e, two_angles_42b6e, true);
NATIVE_ASM(0x42bbe, three_angles_42bbe, true);
// calls $24552 (C) and $39e34; checked by the equivalence run
NATIVE_ASM(0x4217c, view_4217c, false);

}  // namespace native
