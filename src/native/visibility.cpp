// Visibility / depth classification of the object list ($31494 and its
// helpers $31b04-$31c28), called once per frame from the scene update
// ($2f7b8).  Register-convention assembly translated onto the live register
// file; the frame locals of the original (`link a6,#-34`) are real memory
// because the helpers address the caller's frame through a6.
//
// The list at $649ba holds 24-byte entries (word flags at +0, word state
// bits at +2: $4000 = hidden by another object, $8000 = in front); the
// routine finds the nearest drawn entries, tests the remaining ones against
// them with plane/polygon tests and finally compacts the list in place
// ($5d684 receives the sorted copy).
#include "native/asmregs.hpp"
#include "native/orig.hpp"
#include "native/vars.hpp"

namespace native {
using namespace asmr;

namespace {

constexpr uint32_t list_base = 0x649ba;
constexpr uint32_t scenario_base = 0x5b47c;
constexpr uint32_t record_base = 0x5d47c;

int16_t sw(uint32_t a) { return int16_t(rd16(a)); }
int32_t sl(uint32_t a) { return int32_t(rd32(a)); }
int32_t e16(uint16_t v) { return int16_t(v); }
uint32_t FP() { return cpu().a[6]; }
// Frame locals
constexpr int32_t F_X = -2, F_Y = -6, F_Z = -8, F_BX = -12, F_BZ = -16, F_CNT = -18,
                  F_FLAGS = -22, F_HAS = -24, F_FIRST = -26, F_V = -28, F_H = -30, F_SH = -32;
uint16_t RW(int32_t off) { return rd16(FP() + uint32_t(off)); }
void WW(int32_t off, uint32_t v) { wr16(FP() + uint32_t(off), v); }
uint32_t RL(int32_t off) { return rd32(FP() + uint32_t(off)); }
void WL(int32_t off, uint32_t v) { wr32(FP() + uint32_t(off), v); }

void mulsw(int dst, int src) {
  D(dst) = uint32_t(int32_t(int16_t(lo(dst))) * int32_t(int16_t(lo(src))));
}
void negw(int r) { setw(r, uint16_t(-int16_t(lo(r)))); }
void negl(int r) { D(r) = uint32_t(-int32_t(D(r))); }
void movemw(uint32_t a, int r0, int n) {              // movem.w mem,dr..: sign-extends
  for (int i = 0; i < n; ++i) D(r0 + i) = uint32_t(e16(rd16(a + 2u * uint32_t(i))));
}
// subl src,dn; bges; negl dn  (absolute difference): returns |D(n)-src|
void absdiff(int n, uint32_t src) {
  int64_t r = int64_t(int32_t(D(n))) - int64_t(int32_t(src));
  D(n) = uint32_t(r);
  if (r < 0) negl(n);
}
void copy24(uint32_t from, uint32_t to) {            // moveml from,d0-d5; moveml d0-d5,to
  for (int i = 0; i < 6; ++i) D(i) = rd32(from + 4u * uint32_t(i));
  for (int i = 0; i < 6; ++i) wr32(to + 4u * uint32_t(i), D(i));
}
void push16(uint16_t v) { cpu().push16(v); }
uint16_t pop16() { return cpu().pop16(); }

// ---- helpers (a1 = list entry, d1 = flag word) ---------------------------
void h31b22() {
  setw(2, rd16(A(2) + 12));
  extl(2);
  D(3) = rd32(A(2) + 16);
  setw(4, rd16(A(2) + 14));
  extl(4);
}
void h31b04() {
  setw(1, lo(1) & 0xFF00);
  setw(1, lo(1) >> 3);
  A(2) = record_base + uint32_t(e16(lo(1)));
  h31b22();
}
void h31b14() {
  setw(1, lo(1) & 0xFF00);
  setw(1, lo(1) + lo(1));
  A(2) = scenario_base + uint32_t(e16(lo(1)));
  h31b22();
}
void h31b34() {
  uint16_t s5 = lo(5), s6 = lo(6), s7 = lo(7);       // movemw d5-d7,-(sp)
  setw(2, rd16(A(1) + 6));
  extl(2);
  setw(4, rd16(A(1) + 10));
  extl(4);
  setw(1, lo(1) & 15);
  D(2) = uint32_t(asl32(int32_t(D(2)), D(1)));
  D(4) = uint32_t(asl32(int32_t(D(4)), D(1)));
  D(3) = 0;
  D(5) = 0;
  setw(3, rd16(A(1) + 14));
  setw(5, lo(3));
  setw(3, lo(3) >> 8);
  setw(5, lo(5) & 255);
  setw(6, rd16(0x5ac44) & 255);
  setw(7, rd16(0x5ac46) & 255);
  setw(3, lo(3) - lo(6));
  setw(5, lo(5) - lo(7));
  swapw(3);
  swapw(5);
  D(3) = uint32_t(asr32(int32_t(D(3)), 2));
  D(5) = uint32_t(asr32(int32_t(D(5)), 2));
  D(2) -= D(3);
  D(4) -= D(5);
  setw(3, rd16(A(1) + 8));
  extl(3);
  D(3) = uint32_t(asl32(int32_t(D(3)), D(1)));
  D(5) = uint32_t(e16(s5));                          // movemw (sp)+,d5-d7 sign-extends
  D(6) = uint32_t(e16(s6));
  D(7) = uint32_t(e16(s7));
}
void h31b8e() {
  setw(1, rd16(A(1)));
  if (!(lo(1) & 0x10)) {
    setw(1, rd16(A(1) + 14));
    WW(F_H, lo(1));
    WW(F_H, RW(F_H) & 255);
    setw(1, lo(1) >> 8);
    WW(F_V, lo(1));
    return;
  }
  uint32_t a2 = A(2);
  setw(1, lo(1) & 0xFF00);
  setw(1, lo(1) + lo(1));
  A(2) = scenario_base + uint32_t(e16(lo(1)));
  setw(1, rd16(A(2) + 6) & 255);
  WW(F_V, lo(1));
  setw(1, rd16(A(2) + 8) & 255);
  WW(F_H, lo(1));
  A(2) = a2;
}
void h31bfa() {                                      // a2 = record, accumulate offsets
  A(2) += uint32_t(e16(lo(1)));
  D(1) = 0;
  setw(1, rd16(A(2) + 6));
  setw(1, lo(1) & 255);
  setw(1, lo(1) - RW(F_V));
  swapw(1);
  D(1) = uint32_t(asr32(int32_t(D(1)), 2));
  D(2) += D(1);
  D(1) = 0;
  setw(1, rd16(A(2) + 8));
  setw(1, lo(1) & 255);
  setw(1, lo(1) - RW(F_H));
  swapw(1);
  D(1) = uint32_t(asr32(int32_t(D(1)), 2));
  D(4) += D(1);
}
void h31bd8() {
  uint32_t a2 = A(2);
  setw(1, rd16(A(1)));
  setw(1, lo(1) & 0xFF00);
  setw(1, lo(1) >> 3);
  A(2) = record_base;
  h31bfa();
  A(2) = a2;
}
void h31bea() {
  uint32_t a2 = A(2);
  setw(1, rd16(A(1)));
  setw(1, lo(1) & 0xFF00);
  setw(1, lo(1) + lo(1));
  A(2) = scenario_base;
  h31bfa();
  A(2) = a2;
}
void h31c28() {
  D(1) = 0;
  setw(1, rd16(A(1) + 14));
  setw(1, lo(1) >> 8);
  setw(1, lo(1) - RW(F_V));
  swapw(1);
  D(1) = uint32_t(asr32(int32_t(D(1)), 2));
  D(2) += D(1);
  D(1) = 0;
  setw(1, rd16(A(1) + 14));
  setw(1, lo(1) & 255);
  setw(1, lo(1) - RW(F_H));
  swapw(1);
  D(1) = uint32_t(asr32(int32_t(D(1)), 2));
  D(4) += D(1);
}

// position of the entry in a1 (d1 = its flags) -> d2/d3/d4
void entry_position_a() {                            // 31b04/31b14/31b34 by flag bits
  if (lo(1) & 0x10) h31b14();
  else h31b34();
}
void entry_offset() {                                // 31b04+31bd8 / 31b14+31bea / 31b34+31c28
  if (lo(1) & 0x40) { h31b04(); h31bd8(); }
  else if (lo(1) & 0x10) { h31b14(); h31bea(); }
  else { h31b34(); h31c28(); }
}

void visibility_31494() {
  m68k::Cpu& c = cpu();
  c.push32(c.a[6]);                                  // link a6,#-34
  c.a[6] = c.a[7];
  c.a[7] -= 34;
  auto leave = [&] {
    c.a[7] = c.a[6];
    c.a[6] = c.pop32();
  };
  auto to_30490 = [&] { leave(); };
  A(1) = list_base;
  D(5) = 0xFFFFFFFFu;
  while (sw(A(1)) >= 0) {
    setw(5, lo(5) + 1);
    A(1) += 24;
  }
  if (int16_t(lo(5)) < 1) { to_30490(); return; }
  A(1) -= 24;
  // find the last drawn entry that has a mesh with the "solid" bit
  for (;;) {                                         // $314b6
    setw(1, rd16(A(1)));
    A(2) = rd32(A(1) + 2);
    D(7) = rd32(A(2) + 4);
    bool found = false;
    if (int32_t(D(7)) > 0) {
      A(0) = D(7);
      setw(7, rd16(A(0)));
      if (int16_t(lo(7)) >= 0) {
        setw(7, lo(7) & 0x4000);
        if (lo(7) == 0) setw(7, rd16(A(0) + 4));
        else setw(7, rd16(A(0) + 2));
      }
      if (lo(7) != 0xFFFF) {
        setw(7, lo(7) & 4095);
        setb(7, rd8(A(0) + 7 + uint32_t(e16(lo(7)))));
        setb(7, lob(7) & 16);
        found = lob(7) != 0;
      }
    }
    if (found) break;
    int r = int(int16_t(lo(5))) - 1;                 // subqw #1,d5; blts
    setw(5, uint32_t(r));
    if (r < 0) { to_30490(); return; }
    A(1) -= 24;
  }
  wr16(0x6504a, lo(5));                              // $314f6
  A(0) = A(1);
  h31b8e();
  setw(2, rd16(0x5ad6a));
  setw(4, rd16(0x5ad6e));
  negw(2);
  negw(4);
  extl(2);
  extl(4);
  call_c(0x31c50);                                   // d2/d4 -> view-space reference
  WL(F_BX, D(2));
  WL(F_BZ, D(4));
  setw(1, rd16(A(1)));
  if (lo(1) & 0x10) h31b14();
  else h31b34();
  A(3) = D(2);
  A(4) = D(3);
  A(5) = D(4);
  D(7) = 3072;
  for (;;) {                                         // $31540: walk back while near
    int r = int(int16_t(lo(5))) - 1;
    setw(5, uint32_t(r));
    if (r < 0) break;
    A(1) -= 24;
    setw(1, rd16(A(1)));
    if (lo(1) & 0x40) { h31b04(); h31bd8(); }
    else if (lo(1) & 0x10) { h31b14(); h31bea(); }
    else { h31b34(); h31c28(); }
    absdiff(2, A(3));
    if (int32_t(D(2)) > int32_t(D(7))) { A(1) += 24; break; }
    absdiff(3, A(4));
    if (int32_t(D(3)) > int32_t(D(7))) { A(1) += 24; break; }
    absdiff(4, A(5));
    if (int32_t(D(4)) <= int32_t(D(7))) continue;
    A(1) += 24;
    break;
  }
  setw(5, lo(5) + 1);                                // $31594
  wr16(0x6504c, lo(5));
  wr16(0x6504e, 0xFFFF);

  auto set_hidden = [&] { wr16(A(1) + 2, rd16(A(1) + 2) | 0xC000); };
  auto set_front = [&] { wr16(A(1) + 2, rd16(A(1) + 2) | 0x8000); };

  bool first_pass = true;
  for (;;) {                                         // $315aa: classify every entry
    if (!first_pass) A(1) += 24;                     // $315a6
    first_pass = false;
    if (sw(A(1)) < 0) break;                         // -> $31a40
    D(1) = A(1);                                     // movel a1,d1; cmpl a0,d1
    if (A(1) == A(0)) goto next;                     // $31a32
    if (int32_t(A(1)) > int32_t(A(0))) {
      setw(1, rd16(A(0)));
      if (lo(1) & 0x10) {
        h31b14();
      } else {
        std::swap(A(0), A(1));
        h31b34();
        std::swap(A(0), A(1));
      }
      D(5) = D(2);
      D(6) = D(3);
      D(7) = D(4);
      setw(1, rd16(A(1)));
      entry_offset();
      D(0) = 2560;
      absdiff(2, D(5));
      if (int32_t(D(2)) > int32_t(D(0))) break;
      absdiff(3, D(6));
      if (int32_t(D(3)) > int32_t(D(0))) break;
      absdiff(4, D(7));
      if (int32_t(D(4)) > int32_t(D(0))) break;
    }
    {                                                // $3162a
      setw(1, rd16(A(1)));
      enum { HIDDEN, FRONT, NEXT } result = NEXT;
      bool detail = false;
      if (lo(1) & 0x40) {                            // record object
        A(3) = record_base;
        setw(1, lo(1) & 0xFF00);
        setw(1, asr16(lo(1), 3));
        uint32_t o = uint32_t(e16(lo(1)));
        setw(2, rd16(A(3) + 12 + o));
        setw(4, rd16(A(3) + 14 + o));
        WL(F_Y, rd32(A(3) + 16 + o));
        push16(lo(1));
        h31bd8();
        setw(1, pop16());
        WW(F_X, lo(2));
        WW(F_Z, lo(4));
        A(4) = rd32(A(0) + 2);
        A(4) = rd32(A(4) + 16);
        D(0) = rd32(A(4) + 2);
        WL(F_FLAGS, D(0));
        if (int32_t(D(0)) < 0) {
          D(0) &= 0x7FFFFFFFu;
          setw(1, rd16(A(0)));
          setw(1, lo(1) & 0xFF00);
          setw(1, lo(1) >> 3);
          A(2) = scenario_base;
          D(0) += rd32(A(2) + 16 + uint32_t(e16(lo(1))));
          D(1) = uint32_t(-int32_t(rd32(0x5ad70)));
          result = int32_t(D(1)) > int32_t(D(0)) ? FRONT : HIDDEN;
        } else {
          detail = true;
        }
      } else if (lo(1) & 0x10) {                     // scenario object
        A(3) = scenario_base;
        setw(1, lo(1) & 0xFF00);
        setw(1, lo(1) + lo(1));
        uint32_t o = uint32_t(e16(lo(1)));
        setw(2, rd16(A(3) + 12 + o));
        setw(4, rd16(A(3) + 14 + o));
        WL(F_Y, rd32(A(3) + 16 + o));
        push16(lo(1));
        h31bea();
        setw(1, pop16());
        WW(F_X, lo(2));
        WW(F_Z, lo(4));
        setb(0, rd8(A(3) + 4 + uint32_t(e16(lo(1)))));
        setb(0, lob(0) & 0xC0);
        if (lob(0) == 0) {
          detail = true;
        } else {
          A(4) = rd32(A(0) + 2);
          A(4) = rd32(A(4) + 16);
          D(0) = rd32(A(4) + 2);
          WL(F_FLAGS, D(0));
          if (int32_t(D(0)) >= 0) {
            wr16(0x5ac96, 34);
            call_c(0x2f216);
            result = FRONT;
          } else {
            D(0) &= 0x7FFFFFFFu;
            setw(1, rd16(A(0)));
            setw(1, lo(1) & 0xFF00);
            setw(1, lo(1) + lo(1));
            A(2) = scenario_base;
            D(0) += rd32(A(2) + 16 + uint32_t(e16(lo(1))));
            D(1) = uint32_t(-int32_t(rd32(0x5ad70)));
            result = int32_t(D(1)) > int32_t(D(0)) ? FRONT : HIDDEN;
          }
        }
      } else {                                       // plain entry
        h31b34();
        h31c28();
        WW(F_X, lo(2));
        WL(F_Y, D(3));
        WW(F_Z, lo(4));
        detail = true;
      }
      if (detail) {
        // $3173a: test against the mesh header of the object in a0
        A(4) = rd32(A(0) + 2);
        A(4) = rd32(A(4) + 16);
        setw(0, rd16(A(4)));
        A(4) += 2;
        if (int16_t(lo(0)) >= 0) {
          std::swap(A(0), A(1));
          h31b34();
          std::swap(A(0), A(1));
          A(3) = uint32_t(e16(lo(2)));
          A(5) = uint32_t(e16(lo(4)));
          A(4) += 4;
          result = HIDDEN;                           // default when all faces pass
          for (;;) {                                 // $31756/$31758
            A(2) = rd32(A(4));
            for (;;) {
              movemw(A(2), 2, 3);
              movemw(A(2) + 6, 5, 3);
              setw(2, lo(2) + uint16_t(A(3)));
              setw(4, lo(4) + uint16_t(A(5)));
              setw(2, lo(2) - RW(F_X));
              setw(4, lo(4) - RW(F_Z));
              negw(2);
              D(3) = RL(F_Y);
              negw(4);
              mulsw(2, 5);
              mulsw(3, 6);
              mulsw(4, 7);
              D(4) += D(3);
              D(4) += D(2);
              D(1) = D(4);
              movemw(A(2), 2, 3);
              A(2) += 6;
              A(2) += 6;
              setw(2, lo(2) + uint16_t(A(3)));
              setw(4, lo(4) + uint16_t(A(5)));
              extl(2);
              extl(4);
              D(2) -= RL(F_BX);
              D(3) = rd32(0x5ad70);
              D(4) -= RL(F_BZ);
              negl(2);
              negl(3);
              negl(4);
              mulsw(2, 5);
              mulsw(3, 6);
              mulsw(4, 7);
              D(4) += D(3);
              D(4) += D(2);
              bool d4neg = int32_t(D(4)) < 0;
              if (!d4neg && int32_t(D(1)) >= 0) { result = FRONT; goto classified; }
              setw(0, lo(0) - 1);                    // dbf d0,$31758
              if (lo(0) == 0xFFFF) { result = HIDDEN; goto classified; }
            }
          }
        } else {
          // $317c6: indexed face list
          setw(0, lo(0) & 16383);
          WW(F_CNT, lo(0));
          WL(F_FLAGS, rd32(A(4)));
          A(4) += 4;
          uint32_t saved_a4 = A(4);
          setw(1, rd16(A(0)));
          setw(1, lo(1) & 0xFF00);
          setw(1, lo(1) + lo(1));
          A(2) = scenario_base + uint32_t(e16(lo(1)));
          A(4) = rd32(A(4));                         // moveal a4@+,a4
          setw(0, rd16(A(4)));
          setw(0, lo(0) + 164);
          D(1) = uint32_t(-int32_t(rd32(0x5ad70)));
          setb(3, rd8(A(2) + 125));
          setw(3, lo(3) & 15);
          WW(F_SH, lo(3));
          setw(0, rd16(A(2) + 2 + uint32_t(e16(lo(0)))));
          setw(0, asr16(lo(0), D(3)));
          bool to_planes = int16_t(lo(1)) < int16_t(lo(0));
          if (!to_planes) {
            for (;;) {                               // $3180c
              WW(F_HAS, 0);
              WW(F_FIRST, rd16(A(4)));
              if (int16_t(rd16(A(4))) < 0) { to_planes = true; break; }
              for (;;) {                             // $31818
                setw(0, rd16(A(4)));
                A(4) += 2;
                if (int16_t(lo(0)) >= 0) {
                  setw(1, rd16(A(4)));
                  setw(1, lo(1) & 4095);
                } else {
                  setw(0, lo(0) & 4095);
                  setw(1, RW(F_FIRST));
                  WW(F_HAS, RW(F_HAS) + 1);
                }
                setw(0, lo(0) + 164);
                setw(1, lo(1) + 164);
                setw(3, 0xFF00);
                A(5) = A(2) + uint32_t(e16(lo(0)));
                movemw(A(2) + uint32_t(e16(lo(1))), 5, 3);
                setw(5, lo(5) - rd16(A(5)));
                setw(7, lo(7) - rd16(A(5) + 4));
                mulsw(7, 3);
                D(7) = uint32_t(asr32(int32_t(D(7)), 6));
                mulsw(5, 3);
                negl(5);
                D(5) = uint32_t(asr32(int32_t(D(5)), 6));
                std::swap(D(5), D(7));
                movemw(A(5), 2, 3);
                setw(0, RW(F_SH));
                setw(2, asr16(lo(2), D(0)));
                D(3) = uint32_t(asr32(int32_t(D(3)), D(0)));
                setw(4, asr16(lo(4), D(0)));
                setw(2, lo(2) + rd16(A(2) + 12));
                D(3) += rd32(A(2) + 16);
                setw(4, lo(4) + rd16(A(2) + 14));
                setw(2, lo(2) + rd16(0x5ad6a));
                D(3) += rd32(0x5ad70);
                setw(4, lo(4) + rd16(0x5ad6e));
                negw(2);
                negw(3);
                negw(4);
                mulsw(5, 2);
                mulsw(7, 4);
                D(7) += D(5);
                if (int32_t(D(7)) < 0) {             // $318a2
                  if (RW(F_HAS) == 0) continue;      // back to $31818
                  A(4) = saved_a4;
                  result = HIDDEN;
                  goto classified;
                }
                if (RW(F_HAS) != 0) break;           // bnew $3180c
                do {
                  setw(0, rd16(A(4)));
                  A(4) += 2;
                } while (int16_t(lo(0)) >= 0);
                break;                               // braw $3180c
              }
            }
          }
          // $318b0: plane tests of the face list
          A(4) = saved_a4;
          A(4) += 4;
          for (;;) {
            A(2) = rd32(A(4));
            A(4) += 4;
            for (;;) {                               // $318b4
              movemw(A(2) + 2, 2, 3);
              setw(4, lo(4) & 4095);
              setw(2, lo(2) + 164);
              setw(3, lo(3) + 164);
              setw(4, lo(4) + 164);
              setw(1, rd16(A(0)));
              setw(1, lo(1) & 0xFF00);
              setw(1, lo(1) + lo(1));
              A(2) = scenario_base + uint32_t(e16(lo(1)));
              A(5) = A(2) + uint32_t(e16(lo(2)));
              setw(1, lo(4));
              movemw(A(2) + uint32_t(e16(lo(3))), 2, 3);
              setw(2, lo(2) - rd16(A(5)));
              setw(3, lo(3) - rd16(A(5) + 2));
              setw(4, lo(4) - rd16(A(5) + 4));
              movemw(A(2) + uint32_t(e16(lo(1))), 5, 3);
              setw(5, lo(5) - rd16(A(5)));
              setw(6, lo(6) - rd16(A(5) + 2));
              setw(7, lo(7) - rd16(A(5) + 4));
              setw(0, lo(4));
              setw(1, lo(7));
              mulsw(7, 3);
              mulsw(4, 6);
              D(7) -= D(4);
              D(7) = uint32_t(asr32(int32_t(D(7)), 6));
              mulsw(0, 5);
              mulsw(1, 2);
              D(0) -= D(1);
              D(0) = uint32_t(asr32(int32_t(D(0)), 6));
              mulsw(6, 2);
              mulsw(5, 3);
              D(6) -= D(5);
              D(6) = uint32_t(asr32(int32_t(D(6)), 6));
              setw(5, lo(7));
              setw(7, lo(6));
              setw(6, lo(0));
              movemw(A(5), 2, 3);
              setw(0, RW(F_SH));
              setw(2, asr16(lo(2), D(0)));
              D(3) = uint32_t(asr32(int32_t(D(3)), D(0)));
              setw(4, asr16(lo(4), D(0)));
              setw(2, lo(2) + rd16(A(2) + 12));
              D(3) += rd32(A(2) + 16);
              setw(4, lo(4) + rd16(A(2) + 14));
              setw(2, lo(2) - RW(F_X));
              D(3) -= RL(F_Y);
              setw(4, lo(4) - RW(F_Z));
              negw(2);
              negl(3);
              negw(4);
              mulsw(2, 5);
              mulsw(3, 6);
              mulsw(4, 7);
              D(4) += D(3);
              D(4) += D(2);
              D(1) = D(4);
              movemw(A(5), 2, 3);
              setw(2, asr16(lo(2), D(0)));
              D(3) = uint32_t(asr32(int32_t(D(3)), D(0)));
              setw(4, asr16(lo(4), D(0)));
              setw(2, lo(2) + rd16(A(2) + 12));
              D(3) += rd32(A(2) + 16);
              setw(4, lo(4) + rd16(A(2) + 14));
              extl(2);
              extl(4);
              D(2) -= RL(F_BX);
              D(3) += rd32(0x5ad70);
              D(4) -= RL(F_BZ);
              negl(2);
              negl(3);
              negl(4);
              mulsw(2, 5);
              mulsw(3, 6);
              mulsw(4, 7);
              D(4) += D(3);
              D(4) += D(2);
              if (int32_t(D(4)) < 0) {
                int r = int(int16_t(RW(F_CNT))) - 1;
                WW(F_CNT, uint32_t(r));
                if (r >= 0) break;                   // next face: $318b4 (reload a2 below)
                result = HIDDEN;
                goto classified;
              }
              if (int32_t(D(1)) >= 0) { result = FRONT; goto classified; }
              if (int16_t(RW(F_FLAGS)) < 0) {        // $319a8: tstw fp@(-22); bges $31a1c
                A(2) = rd32(A(4) - 4);
                movemw(A(2) + 2, 2, 2);
                setw(2, lo(2) + 164);
                setw(3, lo(3) + 164);
                setw(1, rd16(A(0)));
                setw(1, lo(1) & 0xFF00);
                setw(1, lo(1) + lo(1));
                A(2) = scenario_base + uint32_t(e16(lo(1)));
                A(5) = A(2) + uint32_t(e16(lo(2)));
                movemw(A(2) + uint32_t(e16(lo(3))), 2, 3);
                setw(2, lo(2) - rd16(A(5)));
                setw(3, lo(3) - rd16(A(5) + 2));
                setw(4, lo(4) - rd16(A(5) + 4));
                setw(7, rd16(A(5) + 2));
                mulsw(4, 7);
                D(4) = uint32_t(asr32(int32_t(D(4)), 6));
                negw(7);
                mulsw(7, 2);
                D(7) = uint32_t(asr32(int32_t(D(7)), 6));
                setw(5, lo(4));
                movemw(A(5), 2, 3);
                setw(0, RW(F_SH));
                setw(2, asr16(lo(2), D(0)));
                setw(4, asr16(lo(4), D(0)));
                setw(2, lo(2) + rd16(A(2) + 12));
                setw(4, lo(4) + rd16(A(2) + 14));
                setw(2, lo(2) - RW(F_X));
                setw(4, lo(4) - RW(F_Z));
                negw(2);
                negw(4);
                mulsw(2, 5);
                mulsw(4, 7);
                D(4) += D(2);
                if (int32_t(D(4)) >= 0) { result = FRONT; goto classified; }
              }
              {                                      // $31a1c
                int r = int(int16_t(RW(F_CNT))) - 1;
                WW(F_CNT, uint32_t(r));
                if (r >= 0) break;
                result = HIDDEN;
                goto classified;
              }
            }
          }
        }
      }
    classified:
      if (result == HIDDEN) set_hidden();
      else if (result == FRONT) set_front();
    }
  next:
    wr16(0x6504e, rd16(0x6504e) + 1);
  }
  // $31a40: compact the list ($5d684 gets the sorted copy, a2 the output)
  setw(7, rd16(0x6504e));
  if (int16_t(lo(7)) <= 0) { leave(); return; }
  setw(6, lo(7));
  A(0) = list_base;
  A(1) = 0x5d684;
  setw(0, rd16(0x6504c));
  if (int16_t(lo(0)) < 0) { leave(); return; }
  setw(0, uint16_t(lo(0) << 3));
  setw(1, lo(0));
  setw(0, lo(0) + lo(0));
  setw(0, lo(0) + lo(1));
  A(0) += uint32_t(e16(lo(0)));
  A(2) = A(0);
  A(3) = A(1);
  do {
    copy24(A(0), A(1));
    A(0) += 24;
    A(1) += 24;
    setw(7, lo(7) - 1);
  } while (lo(7) != 0xFFFF);
  A(1) = A(3);
  setw(7, lo(6));
  do {                                               // $31a80
    if (sw(A(3)) >= 0) {
      setw(0, rd16(A(3) + 2));
      setw(0, lo(0) & 0xC000);
      if (lo(0) == 0xC000) {
        wr16(A(3) + 2, rd16(A(3) + 2) & 4095);
        copy24(A(3), A(2));
        wr16(A(3), 0xFFFF);
        A(2) += 24;
      }
    }
    A(3) += 24;
    setw(7, lo(7) - 1);
  } while (lo(7) != 0xFFFF);
  setw(0, rd16(0x6504a));
  setw(0, lo(0) - rd16(0x6504c));
  setw(0, uint16_t(lo(0) << 3));
  setw(1, lo(0));
  setw(0, lo(0) + lo(0));
  setw(0, lo(0) + lo(1));
  copy24(A(1) + uint32_t(e16(lo(0))), A(2));
  A(2) += 24;
  setw(7, lo(6));
  do {                                               // $31ad4
    if (sw(A(1)) >= 0) {
      setw(0, rd16(A(1) + 2));
      setw(0, lo(0) & 0xC000);
      if (lo(0) == 0x8000) {
        wr16(A(1) + 2, rd16(A(1) + 2) & 4095);
        copy24(A(1), A(2));
        A(2) += 24;
      }
    }
    A(1) += 24;
    setw(7, lo(7) - 1);
  } while (lo(7) != 0xFFFF);
  leave();
}
NATIVE_ASM(0x31494, visibility_31494, true);

}  // namespace
}  // namespace native
