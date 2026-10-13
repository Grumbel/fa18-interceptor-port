// Scene list builder of the 3D engine ($30064 and its internal subroutines
// $3034c/$3043c/$30478/$30530/$3067a, the tail at $30b5c-$3127a) and the
// object projection step $30872.  Register-convention assembly translated
// to run on the live register file; the frame of the original ($30064 does
// `link a6,#-60`) is reproduced in memory because the subroutines address
// the caller's locals through a6.
//
// Frame locals:
//   -4 object tables   -8 visibility bitmap   -12 section index table
//   -20 output list    -24/-28 offset tables  -36 count word
//   -38/-40 view angles  -42 view slot  -46 phase counter
//   -50/-54/-58 object/slot/model translation tables
#include "native/asmregs.hpp"
#include "native/orig.hpp"
#include "native/vars.hpp"

namespace native {
using namespace asmr;

namespace {

constexpr uint32_t scenario_base = 0x5b47c;
constexpr uint32_t record_base = 0x5d47c;

[[noreturn]] void error_loop(uint16_t code) {
  wr16(0x5ac96, code);
  for (;;) call_c(0x2f216);
}

int16_t sw(uint32_t a) { return int16_t(rd16(a)); }
int32_t sl(uint32_t a) { return int32_t(rd32(a)); }
// Word add / subtract on the low word of a data register; returns whether
// the true (signed, N xor V) result is negative.
bool addw_neg(int n, uint16_t v) {
  int32_t r = int32_t(int16_t(lo(n))) + int16_t(v);
  setw(n, uint32_t(r));
  return r < 0;
}
bool subw_neg(int n, uint16_t v) {
  int32_t r = int32_t(int16_t(lo(n))) - int16_t(v);
  setw(n, uint32_t(r));
  return r < 0;
}
uint32_t fp() { return cpu().a[6]; }
constexpr int32_t L_TABLES = -4, L_BITMAP = -8, L_SECTIONS = -12, L_OUT = -20, L_OFF1 = -24,
                  L_OFF2 = -28, L_COUNT = -36, L_ANG1 = -38, L_ANG2 = -40, L_SLOT = -42,
                  L_PHASE = -46, L_T50 = -50, L_T54 = -54, L_T58 = -58;
uint32_t FL(int32_t off) { return rd32(fp() + uint32_t(off)); }
void SFL(int32_t off, uint32_t v) { wr32(fp() + uint32_t(off), v); }

// ---- $3067a: 16 words of -1 at stride d1 ------------------------------------
void fill16() {
  for (int i = 0; i < 16; ++i) {
    wr16(A(1), lo(0));
    A(1) += uint32_t(ext(lo(1)));
  }
}

// ---- $3043c: binary search for d1.w in the table at a0 ----------------------
void table_search() {
  D(6) = 0;
  setw(7, rd16(A(0)));
  A(0) += 2;
  setw(7, asr16(lo(7), 1));
  for (;;) {
    setw(0, lo(7));
    if (subw_neg(0, lo(6))) {
      wr16(0x5ac96, 28);
      call_c(0x2f216);
      return;
    }
    setw(0, asr16(lo(0), 1));
    setw(0, lo(0) + lo(6));
    setw(4, lo(0));
    setw(4, lo(4) + lo(4));
    int16_t m = sw(A(0) + uint32_t(ext(lo(4))));
    int16_t key = int16_t(lo(1));
    if (key == m) return;
    if (key < m) {
      setw(7, lo(0));
      setw(7, lo(7) - 1);
    } else {
      setw(6, lo(0));
      setw(6, lo(6) + 1);
    }
  }
}

// ---- $30478: flush the pending slot marks that match (d1.b, d2, d3) ----------
void flush_marks() {
  if (int8_t(lob(1)) < 0) return;
  if (rd8(0x5ab5c) == 0) return;
  wr16(0x5adcc, 80);
  uint32_t saved = D(6);
  A(0) = scenario_base;
  D(6) = 0;
  do {
    uint8_t f = rd8(A(0) + 1);
    if ((f & 0x40) && (f & 0x10) && lo(3) == rd16(A(0) + 6) && lo(2) == rd16(A(0) + 8) &&
        lob(1) == rd8(A(0) + 10)) {
      wr8(A(0) + 1, f & ~0x10);
      wr8(A(2), 16);
      A(2)++;
      wr8(A(2), D(6));
      A(2)++;
      wr8(A(2), 0xFF);
    }
    A(0) += 512;
    setb(6, lob(6) + 1);
  } while (int8_t(lob(6)) < 16);
  A(0) = record_base;
  D(6) = 0;
  do {
    uint8_t f = rd8(A(0) + 1);
    if ((f & 0x40) && (f & 0x10) && lo(3) == rd16(A(0) + 6) && lo(2) == rd16(A(0) + 8) &&
        lob(1) == rd8(A(0) + 10)) {
      wr8(A(0) + 1, f & ~0x10);
      wr8(A(2), 64);
      A(2)++;
      wr8(A(2), D(6));
      A(2)++;
      wr8(A(2), 0xFF);
    }
    A(0) += 32;
    setb(6, lob(6) + 1);
  } while (int8_t(lob(6)) < 16);
  D(6) = saved;
}

// ---- $30530: same, searching the object's entry list for a free slot --------
void place_marks() {
  if (rd8(0x5ab5c) == 0) return;
  wr16(0x5adcc, 81);
  for (int pass = 0; pass < 2; ++pass) {
    const uint32_t base = pass == 0 ? scenario_base : record_base;
    const uint32_t stride = pass == 0 ? 512 : 32;
    const uint8_t marker = pass == 0 ? 16 : 64;
    const uint16_t err = pass == 0 ? 14 : 54;
    setb(1, 0xFF);
    A(0) = base;
    D(6) = 0;
    do {
      uint8_t f = rd8(A(0) + 1);
      if ((f & 0x40) && (f & 0x10) && lo(3) == rd16(A(0) + 6) && lo(2) == rd16(A(0) + 8)) {
        setb(4, rd8(A(0) + 10));
        if (lob(4) != lob(1)) {
          setb(1, lob(4));
          if (int8_t(lob(1)) < 0) error_loop(err);
          extw(4);
          swapw(1);
          setw(4, uint16_t(lo(4) << 5));
          setw(1, lo(4));
          setw(4, lo(4) + lo(4));
          setw(4, lo(4) + lo(1));
          swapw(1);
          A(2) = A(1) + uint32_t(ext(lo(4)));
          wr32(0x5ad26, A(2));
          wr32(0x5ad26, rd32(0x5ad26) + 93);
          for (;;) {
            if (int32_t(A(2)) >= sl(0x5ad26)) return;
            setb(4, rd8(A(2)));
            if (int8_t(lob(4)) < 0) break;
            if ((lob(4) & 0x10) || (lob(4) & 0x40)) A(2) += 2;
            else A(2) += 4;
          }
        }
        wr8(A(0) + 1, rd8(A(0) + 1) & ~0x10);
        wr8(A(2), marker);
        A(2)++;
        wr8(A(2), D(6));
        A(2)++;
        wr8(A(2), 0xFF);
      }
      A(0) += stride;
      setb(6, lob(6) + 1);
    } while (int8_t(lob(6)) < 16);
  }
}

// ---- $3034c: emit the entries of object (d0.w, d1.w) into slot list a1 -------
void emit_object() {
  m68k::Cpu& c = cpu();
  uint32_t saved[10];                          // moveml d2-d5/a0-a5,-(sp)
  for (int i = 0; i < 4; ++i) saved[i] = c.d[2 + i];
  for (int i = 0; i < 6; ++i) saved[4 + i] = c.a[i];
  c.a[7] -= 40;
  for (int i = 0; i < 10; ++i) wr32(c.a[7] + 4u * uint32_t(i), saved[i]);
  auto finish = [&] {
    flush_marks();
    place_marks();
    for (int i = 0; i < 4; ++i) c.d[2 + i] = rd32(c.a[7] + 4u * uint32_t(i));
    for (int i = 0; i < 6; ++i) c.a[i] = rd32(c.a[7] + 16 + 4u * uint32_t(i));
    c.a[7] += 40;
  };
  auto restore_only = [&] {
    place_marks();
    for (int i = 0; i < 4; ++i) c.d[2 + i] = rd32(c.a[7] + 4u * uint32_t(i));
    for (int i = 0; i < 6; ++i) c.a[i] = rd32(c.a[7] + 16 + 4u * uint32_t(i));
    c.a[7] += 40;
  };
  setw(2, lo(0));
  setw(3, lo(1));
  A(0) = FL(L_TABLES);
  setw(0, lo(0) + lo(0));
  A(0) += uint32_t(ext(rd16(A(0) + uint32_t(ext(lo(0))))));
  if (sw(A(0)) < 0) { restore_only(); return; }
  setw(0, uint16_t(lo(0) << 3));
  setw(6, lo(1));
  setw(4, lo(1));
  setw(6, asr16(lo(6), 5));
  setw(6, lo(6) + lo(6));
  setw(6, lo(6) + lo(6));
  setw(6, lo(6) + lo(0));
  setw(4, lo(4) & 31);
  A(5) = FL(L_BITMAP);
  D(5) = rd32(A(5) + uint32_t(ext(lo(6))));
  if (!((D(5) >> (D(4) & 31)) & 1)) { restore_only(); return; }
  A(5) = A(0);
  A(5) += uint32_t(ext(rd16(A(0))));
  A(5) += 2;
  table_search();
  setw(0, lo(0) + lo(0));
  setw(0, lo(0) + lo(0));
  A(5) = rd32(A(5) + uint32_t(ext(lo(0))));
  setb(1, 0xFF);
  for (;;) {                                   // $3039a
    setb(0, rd8(A(5)));
    A(5)++;
    if (lob(0) == 0xFF) break;
    setb(6, lob(0));
    setb(0, lob(0) & 15);
    if (lob(1) != lob(0)) {                    // cmpb d0,d1; beqs 303d4
      flush_marks();
      D(5) = 16;
      setb(1, lob(0));
      extw(0);
      setw(0, uint16_t(lo(0) << 5));
      setw(7, lo(0));
      setw(0, lo(0) + lo(0));
      setw(0, lo(0) + lo(7));
      A(2) = A(1) + uint32_t(ext(lo(0)));
      wr32(0x5ad26, A(2));
      wr32(0x5ad26, rd32(0x5ad26) + 93);
    }
    int sub = int(int8_t(lob(5))) - 1;         // subqb #1,d5; blts
    setb(5, uint8_t(sub));
    if (sub < 0) error_loop(56);
    if (int32_t(A(2)) >= sl(0x5ad26)) break;
    setb(7, rd8(A(5)));
    A(5)++;
    setb(0, lob(7));
    setb(7, lob(7) & 0x80);
    wr8(A(2), lob(7));
    A(2)++;
    setb(0, lob(0) & 0x7F);
    wr8(A(2), lob(0));
    A(2)++;
    if (lob(7) & 0x80) {
      setb(6, lob(6) >> 4);
      setw(6, lo(6) & 15);
      setw(6, lo(6) + lo(6));
      A(3) = 0x3080e;
      setb(0, rd8(A(3) + uint32_t(ext(lo(6)))));
      extw(0);
      wr16(A(2), lo(0));
      A(2) += 2;
      setb(0, rd8(A(3) + 1 + uint32_t(ext(lo(6)))));
      wr16(A(2), lo(0));
      A(2) += 2;
    } else {
      wr32(A(2), rd32(A(5)));
      A(2) += 4;
      A(5) += 4;
    }
    wr8(A(2), 0xFF);
  }
  finish();
}

// ---- the object list expansion loop ($30b5c-$3127a) -----------------------------
void expand_list() {
  wr8(fp() + uint32_t(L_PHASE), 0);              // clrb fp@(-46)
  if (rd8(0x5ab5e) == 0) wr16(FL(L_COUNT), 0);
  A(0) = cpu().pop32();
  wr16(0x63c78, 0);
  A(2) = FL(L_OUT);
  A(5) = 0x5acc6;
  wr8(0x5ab54, 0);
  wr8(0x5ab56, 0);
  wr8(0x5ab5f, 0);
  A(4) = 0x5d684;
  for (;;) {                                    // $30b98
    setb(0, rd8(A(0)));
    A(0)++;
    if (int8_t(lob(0)) < 0) break;              // -> $310c0
    if (int8_t(lob(0)) > 20) error_loop(17);
    extw(0);
    A(1) = FL(L_T58);
    setb(0, rd8(A(1) + uint32_t(ext(lo(0)))));
    extw(0);
    setw(1, lo(0));
    A(1) = 0x306bc;
    setw(1, lo(1) + lo(1));
    setb(2, rd8(A(1) + uint32_t(ext(lo(1)))));
    setb(3, rd8(A(1) + 1 + uint32_t(ext(lo(1)))));
    extw(2);
    extw(3);
    if (addw_neg(2, rd16(0x5ac44))) setw(2, lo(2) + 128);
    if (addw_neg(3, rd16(0x5ac46))) setw(3, lo(3) + 128);
    setw(2, uint16_t(lo(2) << 8));
    D(3) &= 255;
    setw(3, lo(3) + lo(2));
    wr32(0x5acb4, D(3));
    setw(0, uint16_t(lo(0) << 2));
    A(1) = FL(L_OFF1);
    D(2) = uint32_t(ext(rd16(A(1) + uint32_t(ext(lo(0))))));
    D(3) = uint32_t(ext(rd16(A(1) + uint32_t(ext(lo(0))) + 2)));
    if (rd8(0x5ab5c) != 0) {
      A(1) = 0x3073a;
      setb(6, rd8(0x5ab48));
      extw(6);
      setw(6, lo(6) + lo(6));
      setw(6, lo(6) + lo(6));
      D(0) = uint32_t(ext(rd16(A(1) + uint32_t(ext(lo(6))))));
      D(1) = uint32_t(ext(rd16(A(1) + uint32_t(ext(lo(6))) + 2)));
      D(2) += D(0);
      D(3) += D(1);
    }
    D(2) = uint32_t(asl32(int32_t(D(2)), 2));
    D(3) = uint32_t(asl32(int32_t(D(3)), 2));
    setb(0, rd8(A(0)));                         // $30c22: repeat count
    A(0)++;
    for (;;) {                                  // $30c24: one instance
      setb(1, rd8(A(0)));
      A(0)++;
      if (int8_t(lob(1)) > 15) error_loop(18);
      A(1) = FL(L_T50);
      extw(1);
      setb(1, rd8(A(1) + uint32_t(ext(lo(1)))));
      setb(6, lob(1));
      extw(6);
      wr16(0x5acb4, lo(6));
      setw(6, lo(6) + lo(6));
      setw(6, lo(6) + lo(6));
      A(1) = FL(L_OFF2);
      D(4) = uint32_t(ext(rd16(A(1) + uint32_t(ext(lo(6))))));
      D(5) = uint32_t(ext(rd16(A(1) + uint32_t(ext(lo(6))) + 2)));
      D(4) = uint32_t(asl32(int32_t(D(4)), 2));
      D(5) = uint32_t(asl32(int32_t(D(5)), 2));
      extw(1);
      setw(1, uint16_t(lo(1) << 5));
      setw(6, lo(1));
      setw(1, lo(1) + lo(1));
      setw(1, lo(1) + lo(6));
      A(3) = A(4) + uint32_t(ext(lo(1)));
      wr32(0x5ad26, A(3));
      wr32(0x5ad26, rd32(0x5ad26) + 96);
      wr32(A(5), A(2));
      D(1) = 0;
      for (;;) {                                // $30c76: one entry
        if (int32_t(A(3)) >= sl(0x5ad26)) break;
        if (int8_t(rd8(A(3))) == -1) break;
        setb(1, lob(1) + 1);
        setw(6, rd16(A(3)));
        A(3) += 2;
        setw(7, lo(6));
        setw(6, lo(6) & 0xFF);
        wr16(0x5acac, lo(6));
        setw(7, lo(7) >> 8);
        wr8(0x5ab52, lob(7));
        setw(6, uint16_t(lo(6) << 8));
        if (int16_t(lo(6)) < 0) error_loop(38);
        setb(6, lob(6) | lob(7));
        wr16(A(2), lo(6));
        A(2) += 2;
        if (lo(7) & 0x40) {
          A(1) = record_base;
          setw(6, lo(6) & 0xFF00);
          setw(6, lo(6) >> 3);
          setw(6, rd16(A(1) + 2 + uint32_t(lo(6))));
        } else {
          setw(6, lo(6) >> 8);
        }
        swapw(7);
        setw(6, lo(6) + lo(6));
        setw(6, lo(6) + lo(6));
        setw(7, lo(6));
        setw(6, lo(6) + lo(6));
        setw(6, lo(6) + lo(6));
        setw(6, lo(6) + lo(7));
        swapw(7);
        A(1) = 0x36094;
        A(1) += uint32_t(ext(lo(6)));
        wr32(A(2), A(1));
        A(2) += 4;
        D(6) = 0;
        if (!(lo(7) & 0x40) && !(lo(7) & 0x10)) {
          setw(6, rd16(A(3)));
          A(3) += 2;
        }
        if (rd8(0x5ab5c) == 0) {
          setw(6, lo(6) + lo(6));
          setw(6, lo(6) + lo(6));
        }
        {
          uint32_t keep = A(1);
          A(1) = 0x3073a;
          setb(7, rd8(0x5ab48));
          extw(7);
          setw(7, lo(7) + lo(7));
          setw(7, lo(7) + lo(7));
          setw(7, rd16(A(1) + uint32_t(ext(lo(7)))));
          A(1) = keep;
        }
        extl(7);
        D(7) = uint32_t(asl32(int32_t(D(7)), 2));
        D(6) -= D(7);
        D(6) += D(4);
        D(6) += D(2);
        wr32(0x5a9e6, D(6));
        cpu().push32(D(6));
        D(6) = 0;
        const uint8_t f52 = rd8(0x5ab52);
        if ((f52 & 0x40) || (f52 & 0x10)) {
          wr16(0x5a9ea, 0);
        } else {
          A(1) = rd32(A(1) + 4);
          wr16(0x5a9ea, 0);
          setw(6, rd16(A(3)));
          A(3) += 2;
        }
        if (rd8(0x5ab5c) == 0) {
          setw(6, lo(6) + lo(6));
          setw(6, lo(6) + lo(6));
        }
        A(1) = 0x3073a;
        setb(7, rd8(0x5ab48));
        extw(7);
        setw(7, lo(7) + lo(7));
        setw(7, lo(7) + lo(7));
        setw(7, rd16(A(1) + 2 + uint32_t(ext(lo(7)))));
        extl(7);
        D(7) = uint32_t(asl32(int32_t(D(7)), 2));
        D(6) -= D(7);
        D(6) += D(5);
        D(6) += D(3);
        wr32(0x5a9ee, D(6));
        D(7) = cpu().pop32();
        cpu().push16(lo(0));
        if (f52 & 0x10) {
          if (f52 & 0x40) {
            A(1) = record_base;
            setw(0, rd16(0x5acac));
            setw(0, uint16_t(lo(0) << 5));
            A(1) += uint32_t(ext(lo(0)));
          } else {
            A(1) = scenario_base;
            setw(0, rd16(0x5acac));
            setw(0, uint16_t(lo(0) << 8));
            setw(0, lo(0) + lo(0));
            A(1) += uint32_t(ext(lo(0)));
            if (rd8(0x5ab5e) == 0) wr8(A(1) + 1, rd8(A(1) + 1) & ~4);
          }
          setw(0, rd16(A(1) + 12) & 4095);
          extl(0);
          D(7) += D(0);
        }
        D(0) = uint32_t(ext(rd16(0x5ad6a)));
        D(7) += D(0);
        if (int32_t(D(7)) < 0) D(7) = uint32_t(-int32_t(D(7)));
        D(7) = uint32_t(asr32(int32_t(D(7)), 8));
        D(7) = uint32_t(asr32(int32_t(D(7)), 4));
        if ((f52 & 0x40) || (f52 & 0x10)) {
          setw(0, rd16(A(1) + 14) & 4095);
          extl(0);
          D(6) += D(0);
        }
        D(0) = uint32_t(ext(rd16(0x5ad6e)));
        D(6) += D(0);
        if (int32_t(D(6)) < 0) D(6) = uint32_t(-int32_t(D(6)));
        D(6) = uint32_t(asr32(int32_t(D(6)), 8));
        D(6) = uint32_t(asr32(int32_t(D(6)), 4));
        D(0) = rd32(0x5ad70);
        if ((f52 & 0x40) || (f52 & 0x10)) {
          D(0) = rd32(A(1) + 16);
          D(0) += rd32(0x5ad70);
          if (int32_t(D(0)) < 0) D(0) = uint32_t(-int32_t(D(0)));
        } else {
          D(0) = uint32_t(-int32_t(D(0)));
        }
        D(0) = uint32_t(asr32(int32_t(D(0)), 8));
        D(0) = uint32_t(asr32(int32_t(D(0)), 3));
        if (int16_t(lo(7)) < int16_t(lo(6))) {
          setw(7, lo(6));
          if (int16_t(lo(7)) < int16_t(lo(0))) setw(7, lo(0));
        } else if (int16_t(lo(7)) < int16_t(lo(0))) {
          setw(7, lo(0));
        }
        setw(7, lo(7) >> 1);
        if (int16_t(lo(7)) > 239) {
          wr16(0x5ac96, 15);
          call_c(0x2f216);
          setw(7, 239);
        }
        A(1) = 0x30e9a;
        setb(6, rd8(A(1) + uint32_t(ext(lo(7)))));
        wr8(A(2) - 5, rd8(A(2) - 5) | lob(6));
        extw(6);
        // $30f96
        D(0) = uint32_t(asr32(sl(0x5a9e6), D(6)));
        wr16(A(2), lo(0));
        A(2) += 2;
        setw(0, rd16(0x5a9ea));
        setw(0, asr16(lo(0), D(6)));
        wr16(A(2), lo(0));
        A(2) += 2;
        D(0) = uint32_t(asr32(sl(0x5a9ee), D(6)));
        wr16(A(2), lo(0));
        A(2) += 2;
        setw(0, cpu().pop16());
        wr8(0x5ab56, rd8(0x5ab56) + 1);
        D(7) = 0;
        if (!(f52 & 0x40) && !(f52 & 0x10)) {
          setb(7, lob(1));
          setw(7, uint16_t(lo(7) << 4));
          swapw(7);
          D(7) |= rd32(0x5acb4);
        } else {
          setb(7, lob(7) | rd8(0x5acad));
          D(7) = (D(7) >> 8) | (D(7) << 24);
          D(7) |= 0x80000000u;
        }
        wr32(A(2), D(7));
        A(2) += 4;
        wr16(A(2), 0);
        A(2) += 2;
        setb(6, rd8(fp() + uint32_t(L_PHASE)));
        wr8(A(2), lob(6));
        A(2)++;
        {
          int ph = int(int8_t(lob(6))) - 1;
          setb(6, uint8_t(ph));
          if (ph < 0) setb(6, 3);
          wr8(fp() + uint32_t(L_PHASE), lob(6));
        }
        wr8(A(2), 0);
        A(2)++;
        wr32(A(2), 0);
        A(2) += 4;
        swapw(7);
        wr16(0x5acb4, lo(7));
        swapw(7);
        wr16(0x63c78, rd16(0x63c78) + 1);
        if (rd8(0x5ab5e) == 0) {
          A(1) = 0x31072;
          D(6) = uint32_t(-int32_t(rd32(0x5ad70)));
          D(6) = uint32_t(asr32(int32_t(D(6)), 8));
          D(6) = uint32_t(asr32(int32_t(D(6)), 3));
          if (int32_t(D(6)) > 3) {
            setw(6, 70);
          } else {
            D(6) += D(6);
            D(6) += D(6);
            if (rd8(0x5aa7e) != 0) setw(6, lo(6) + 2);
            setw(6, rd16(A(1) + D(6)));
          }
          if (int16_t(lo(6)) < int16_t(rd16(0x63c78))) {
            A(1) = FL(L_COUNT);
            wr16(A(1), rd16(A(1)) + 24);
          }
        }
        if (int16_t(rd16(0x63c78)) >= 70) A(2) -= 24;
      }
      // $31082: end of this instance's entries
      if (rd8(0x5ab5c) != 0 && rd8(0x5ab5e) == 0) {
        if (int8_t(lob(1)) <= 1) {
          wr32(A(5), 0xFFFFFFFFu);
        } else {
          A(5) += 4;
          wr16(A(5), lo(1));
          A(5) += 2;
          wr32(A(5), 0xFFFFFFFFu);
          wr8(0x5ab54, rd8(0x5ab54) + 1);
        }
      }
      int cnt = int(int8_t(lob(0))) - 1;        // $310b2: subqb #1,d0; bgtw
      setb(0, uint8_t(cnt));
      if (cnt > 0) continue;
      break;
    }
    A(4) += 1536;
  }
}

// ---- $310c0: expand the visible objects' meshes into the slot tables --------
void expand_meshes() {
  wr16(A(2), 0xFFFF);
  wr8(0x5aa9d, 1);
  wr8(0x5ab55, rd8(0x5ab54));
  if (rd8(0x5ab5c) != 0) {
    D(7) = 412090;
    if (int32_t(D(7)) > int32_t(A(2))) error_loop(16);
    if (D(7) != A(2)) {
      A(2) -= 24;
      wr32(0x5acbe, A(2));
      setb(0, 0);
      wr8(0x5ab57, 0);
      for (;;) {                                // $31112
        setw(7, rd16(A(2)));
        A(2) += 2;
        A(1) = rd32(A(2));
        A(2) += 4;
        D(2) = uint32_t(ext(rd16(A(2))));
        D(3) = uint32_t(ext(rd16(A(2) + 2)));
        D(4) = uint32_t(ext(rd16(A(2) + 4)));
        A(2) += 6;
        D(6) = rd32(A(2));
        A(2) += 4;
        bool skip = (lo(7) & 0x40) || (lo(7) & 0x10);
        bool ok = false;
        if (!skip) {
          D(1) = rd32(A(1) + 4);
          if (int32_t(D(1)) > 0) {
            A(0) = D(1);
            setw(1, rd16(A(0)));
            if (int16_t(lo(1)) >= 0) {
              if ((lo(1) & 0x4000) == 0) setw(1, rd16(A(0) + 4));
              else setw(1, rd16(A(0) + 2));
            }
            if (lo(1) != 0xFFFF) {
              setw(1, lo(1) & 4095);
              setw(1, rd16(A(0) + uint32_t(ext(lo(1)))));
              if (int16_t(lo(1)) >= 0) ok = true;
            }
          }
          if (ok) {
            A(0) += uint32_t(ext(lo(1)));
            setw(7, lo(7) & 15);
            setw(2, asl16(lo(2), D(7)));
            setw(3, asl16(lo(3), D(7)));
            setw(4, asl16(lo(4), D(7)));
            setw(2, lo(2) & 16383);
            setw(4, lo(4) & 16383);
            extw(0);
            setw(1, lo(0));
            A(1) = 0x62a84;
            setw(1, lo(1) + lo(1));
            setw(1, lo(1) + lo(1));
            wr32(A(1) + uint32_t(ext(lo(1))), A(0));
            A(1) = 0x62ab0;
            setw(1, uint16_t(lo(1) << 6));
            A(1) += uint32_t(ext(lo(1)));
            wr32(A(1), D(6));
            A(1) += 4;
            auto cpy = [&] { wr16(A(1), rd16(A(0))); A(1) += 2; A(0) += 2; };
            auto add = [&](int r) {
              setw(6, rd16(A(0)));
              A(0) += 2;
              setw(6, lo(6) + lo(r));
              wr16(A(1), lo(6));
              A(1) += 2;
            };
            cpy();
            add(2); add(2); add(4); add(4);
            for (;;) {                          // $311aa
              cpy();
              add(2); add(2); add(4); add(4);
              D(1) = rd32(A(0));
              A(0) += 4;
              if (int32_t(D(1)) > 0) {
                cpu().push32(A(1));
                A(1) += 4;
              } else {
                wr32(A(1), D(1));
                A(1) += 4;
              }
              setw(7, rd16(A(0)));
              A(0) += 2;
              setw(7, lo(7) - 1);
              do {
                add(2);
                setw(6, rd16(A(0)));
                A(0) += 2;
                wr16(A(1), lo(6));
                A(1) += 2;
                add(4);
                A(0) += 6;
                setw(7, lo(7) - 1);
              } while (lo(7) != 0xFFFF);
              if (int32_t(D(1)) <= 0) break;
              D(7) = cpu().pop32();
              wr32(D(7), A(1));
              A(0) = D(1);
            }
          } else {
            extw(0);
            setw(1, lo(0));
            A(1) = 0x62a84;
            setw(1, lo(1) + lo(1));
            setw(1, lo(1) + lo(1));
            wr32(A(1) + uint32_t(ext(lo(1))), 0xFFFFFFFFu);
          }
          setb(0, lob(0) + 1);                  // $31210
          if (int8_t(lob(0)) >= 11) break;
        }
        wr8(0x5ab57, rd8(0x5ab57) + 1);         // $31218
        setb(7, rd8(0x5ab57));
        if (int8_t(lob(7)) >= int8_t(rd8(0x5ab56))) break;
        A(2) -= 40;
      }
    }
  }
  if (rd8(0x5ab5c) != 0 && rd8(0x5ab5e) == 0) {   // $31234
    A(0) = 0x63c7a;
    wr16(A(0), 0xFFFF);
    A(2) = scenario_base;
    setw(0, 0);
    do {
      uint8_t f = rd8(A(2) + 1);
      if ((f & 0x40) && (f & 4)) {
        wr16(A(0), lo(0));
        A(0) += 2;
        wr16(A(0), 0xFFFF);
      }
      setw(0, lo(0) + 1);
      A(2) += 512;
    } while (int16_t(lo(0)) < 16);
  }
}

// ---- $30064 -----------------------------------------------------------------
void scene_build() {
  m68k::Cpu& c = cpu();
  c.push32(c.a[6]);                              // link a6,#-60
  c.a[6] = c.a[7];
  c.a[7] -= 60;
  wr8(0x5ab4e, 0);
  if (rd8(0x5ab5c) != 0) {
    SFL(L_SECTIONS, rd8(0x5aa7e) ? 353610 : 354220);
    SFL(L_OUT, 412090);
    SFL(L_COUNT, 371880);
    SFL(L_TABLES, 358284);
    SFL(L_BITMAP, 182116);
    SFL(L_OFF1, 198522);
    SFL(L_OFF2, 198606);
    wr16(c.a[6] + uint32_t(L_ANG1), rd16(0x5ac44));
    wr16(c.a[6] + uint32_t(L_ANG2), rd16(0x5ac46));
    wr8(c.a[6] + uint32_t(L_SLOT), rd8(0x5ab49));
  } else {
    SFL(L_SECTIONS, 354220);
    if (rd8(0x5ab5d) == 0 && sl(0x5ad5e) > -40960) {
      SFL(L_SECTIONS, 357262);
      wr8(0x5ab4e, rd8(0x5ab4e) + 1);
    } else if (rd8(0x5aa7e) != 0) {
      SFL(L_SECTIONS, 353610);
    }
    if (rd8(0x5ab5d) == 0) {
      SFL(L_TABLES, 357772);
      SFL(L_BITMAP, 178020);
      SFL(L_OUT, 408730);
      SFL(L_COUNT, 371876);
    } else {
      SFL(L_TABLES, 358028);
      SFL(L_BITMAP, 180068);
      SFL(L_OUT, 410410);
      SFL(L_COUNT, 371878);
    }
    SFL(L_OFF1, 198374);
    SFL(L_OFF2, 198458);
    wr16(c.a[6] + uint32_t(L_ANG1), rd16(0x5ac40));
    wr16(c.a[6] + uint32_t(L_ANG2), rd16(0x5ac42));
    wr8(c.a[6] + uint32_t(L_SLOT), rd8(0x5ab48));
  }
  A(1) = 0x5646c;
  setb(0, rd8(c.a[6] + uint32_t(L_SLOT)));
  extw(0);
  setb(0, rd8(A(1) + uint32_t(ext(lo(0)))));
  extw(0);
  setw(0, uint16_t(lo(0) << 3));
  A(1) = 0x5647c;
  A(1) += uint32_t(ext(lo(0)));
  SFL(L_T54, A(1));
  setw(1, lo(0));
  setw(0, lo(0) + lo(0));
  A(1) = 0x564ac;
  A(1) += uint32_t(ext(lo(0)));
  SFL(L_T50, A(1));
  setw(0, lo(0) + lo(1));
  A(1) = 0x564ec;
  A(1) += uint32_t(ext(lo(0)));
  SFL(L_T58, A(1));
  A(1) = 0x5d684;
  setw(0, 0xFFFF);
  setw(1, 96);
  D(2) = 13;
  do {
    fill16();
    setw(2, lo(2) - 1);
  } while (lo(2) != 0xFFFF);
  if (rd8(0x5ab4e) == 0) {
    setb(0, rd8(c.a[6] + uint32_t(L_SLOT)));
    extw(0);
    A(0) = 0x5649c;
    setb(0, rd8(A(0) + uint32_t(ext(lo(0)))));
    extw(0);
    if (rd8(0x5aa7e) != 0) {
      A(0) = 0x5649c;
      setb(0, rd8(A(0) + uint32_t(ext(lo(0)))));
      extw(0);
      setw(0, lo(0) + lo(0));
    } else {
      setw(0, uint16_t(lo(0) << 4));
      setb(1, rd8(0x5ab4c));
      A(0) = FL(L_T54);
      extw(1);
      setb(1, rd8(A(0) + uint32_t(ext(lo(1)))));
      extw(1);
      setw(1, lo(1) + lo(1));
      setw(0, lo(0) + lo(1));
    }
  } else {
    setb(0, rd8(0x5ab48));
    extw(0);
    setw(1, lo(0));
    A(0) = 0x5649c;
    setb(0, rd8(A(0) + uint32_t(ext(lo(0)))));
    extw(0);
    setw(0, uint16_t(lo(0) << 5));
    A(1) = 0x5646c;
    setb(1, rd8(A(1) + uint32_t(ext(lo(1)))));
    extw(1);
    setw(1, uint16_t(lo(1) << 4));
    A(1) = 0x564ac;
    A(1) += uint32_t(ext(lo(1)));
    setb(1, rd8(0x5ab49));
    extw(1);
    setb(1, rd8(A(1) + uint32_t(ext(lo(1)))));
    extw(1);
    setw(1, lo(1) + lo(1));
    setw(0, lo(0) + lo(1));
  }
  A(0) = FL(L_SECTIONS);
  setw(0, rd16(A(0) + uint32_t(ext(lo(0)))));
  if (int16_t(lo(0)) <= 0) error_loop(66);
  A(0) += uint32_t(ext(lo(0)));
  c.push32(A(0));
  A(3) = 0x5d684;
  D(4) = 14;
  for (;;) {                                     // $30290
    setb(0, rd8(A(0)));
    A(0)++;
    if (int8_t(lob(0)) < 0) {
      wr16(0x5adcc, 82);
      break;
    }
    setw(4, lo(4) - 1);
    if (int16_t(lo(4)) < 0) error_loop(13);
    if (int8_t(lob(0)) > 20) error_loop(12);
    extw(0);
    A(2) = FL(L_T58);
    setb(0, rd8(A(2) + uint32_t(ext(lo(0)))));
    extw(0);
    setw(0, lo(0) + lo(0));
    A(2) = 0x306bc;
    setw(3, rd8(0x5ab5c) == 0 ? 30 : 127);
    setb(2, rd8(A(2) + uint32_t(ext(lo(0)))));
    extw(2);
    setw(1, rd16(c.a[6] + uint32_t(L_ANG1)));
    if (addw_neg(1, lo(2))) {
      setw(1, lo(3));
    } else if (int16_t(lo(1)) > int16_t(lo(3))) {
      setw(1, 0);
    }
    setb(2, rd8(A(2) + 1 + uint32_t(ext(lo(0)))));
    extw(2);
    setw(0, rd16(c.a[6] + uint32_t(L_ANG2)));
    if (addw_neg(0, lo(2))) {
      setw(0, lo(3));
    } else if (int16_t(lo(0)) > int16_t(lo(3))) {
      setw(0, 0);
    }
    A(1) = A(3);
    emit_object();
    setb(0, rd8(A(0)));
    A(0)++;
    extw(0);
    A(0) += uint32_t(ext(lo(0)));
    A(3) += 1536;
  }
  expand_list();
  expand_meshes();
  c.a[7] = c.a[6];                               // unlk a6
  c.a[6] = c.pop32();
}
NATIVE_ASM(0x30064, scene_build, true);

// ---- $30872 -----------------------------------------------------------------
// Camera-relative projection step: (d2, d3, d4) += camera offset, scaled by
// the shift in $5adb0, then falls through into the hypot at $308cc.
void project_30872() {
  m68k::Cpu& c = cpu();
  c.a[7] -= 8;
  wr32(c.a[7], D(5));
  wr32(c.a[7] + 4, D(7));
  setw(1, rd16(0x5ad6a));
  D(6) = rd32(0x5ad70);
  setw(7, rd16(0x5ad6e));
  setw(5, rd16(0x5adb0));
  setw(1, asr16(lo(1), D(5)));
  D(6) = uint32_t(asr32(int32_t(D(6)), D(5)));
  setw(7, asr16(lo(7), D(5)));
  setw(2, lo(2) + lo(1));
  if (lo(2) & 0x8000) setw(2, uint16_t(-int16_t(lo(2))));
  if (rd8(0x5abb3) != 0) {
    D(3) = rd32(0x5ae34);
    D(3) = uint32_t(asr32(int32_t(D(3)), 8));
  } else {
    extl(3);
    D(3) += D(6);
  }
  if (int32_t(D(3)) < 0) D(3) = uint32_t(-int32_t(D(3)));
  if (int32_t(D(3)) >= 524272) {
    setw(1, 32767);
    wr16(0x5ae38, lo(1));
    D(5) = rd32(c.a[7]);
    D(7) = rd32(c.a[7] + 4);
    c.a[7] += 8;
    return;
  }
  setw(4, lo(4) + lo(7));
  if (lo(4) & 0x8000) setw(4, uint16_t(-int16_t(lo(4))));
  setw(2, asr16(lo(2), 4));
  D(3) = uint32_t(asr32(int32_t(D(3)), 4));
  setw(4, asr16(lo(4), 4));
  D(5) = rd32(c.a[7]);
  D(7) = rd32(c.a[7] + 4);
  c.a[7] += 8;
  hypot_308cc();
}
NATIVE_ASM(0x30872, project_30872, true);

}  // namespace
}  // namespace native
