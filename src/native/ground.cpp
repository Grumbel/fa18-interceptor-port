// Terrain of the 3D view ($3f284): the water polygons of the near (32x32
// sectors) and far (8x8 tiles of 2^28) grids, emitted through the polygon
// rasteriser $38c0c.  See docs/ground_sea_sky.md; the tables are decoded
// by terrain.cpp.  Register-convention assembly on the live register file
// with the frame of the original (`link a6,#-70`).
//
// Frame locals:
//   -2 w  camera height term (unused by the passes)
//   -8/-6/-4 w  matrix column 1 x camera height (y contribution)
//   -32 w  vertex precision shift   -34 w  coarse-mesh distance test on
//   -36 b  coarse mesh for the own tile ($5ab94)   -40 l  scaled altitude
//   -44/-42 w  camera tile (x, z)   -48/-46 w  grid limits
//   -52 l  grid table   -62 w  1 = near pass   -64 w  tile shift
//   -68 w  last pattern entry seen
#include "native/asmregs.hpp"
#include "native/orig.hpp"
#include "native/vars.hpp"

namespace native {
using namespace asmr;

namespace {

uint32_t FP() { return cpu().a[6]; }
uint16_t RW(int32_t off) { return rd16(FP() + uint32_t(off)); }
void WW(int32_t off, uint32_t v) { wr16(FP() + uint32_t(off), v); }
uint32_t RL(int32_t off) { return rd32(FP() + uint32_t(off)); }
void WL(int32_t off, uint32_t v) { wr32(FP() + uint32_t(off), v); }
uint8_t RB(int32_t off) { return rd8(FP() + uint32_t(off)); }
void WB(int32_t off, uint32_t v) { wr8(FP() + uint32_t(off), v); }
int16_t sw(uint32_t a) { return int16_t(rd16(a)); }
// cmpil #imm,fp@(-40); bgt
bool alt_above(int32_t imm) { return int32_t(RL(-40)) > imm; }
uint32_t rol32(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
void negw(int r) { setw(r, uint16_t(-int16_t(lo(r)))); }
void negl(int r) { D(r) = uint32_t(-int32_t(D(r))); }
uint32_t muls(uint16_t a, int16_t b) { return uint32_t(int32_t(int16_t(a)) * b); }
// addw dS,dD; blt: the exact (signed) sum is negative
bool addw_neg(int d, uint16_t v) {
  int32_t r = int32_t(int16_t(lo(d))) + int16_t(v);
  setw(d, uint32_t(r));
  return r < 0;
}
// 32768 / max($5ad3a, 2) as in $3f2d4 / $3f682 (divu.w)
void scale_divisor() {
  D(7) = 32768;
  if (int16_t(rd16(0x5ad3a)) >= 2) divuw(D(7), rd16(0x5ad3a));
  else divuw(D(7), 2);
}

// ---- $3f364: one pass (far or near grid) --------------------------------------
void terrain_pass() {
  m68k::Cpu& c = cpu();
  if (rd8(0x5aa7d) != 0) {
    A(4) = 0x5af36;
  } else {
    A(4) = 0x5b47c + uint32_t(ext(rd16(0x5abd6)));
    A(4) += 20;
  }
  D(2) = rd32(A(4) + 4);
  D(2) += 4096;
  swapw(2);
  D(2) = rol32(D(2), 4);
  negl(2);
  WW(-2, lo(2));
  D(0) = rd32(A(4));
  D(1) = rd32(A(4) + 8);
  swapw(0);
  swapw(1);
  setw(3, lo(0));
  setw(4, lo(1));
  setw(2, RW(-64));
  {
    uint32_t n = D(2) & 63;                           // lsrw d2,d0 / d1
    setw(0, n > 15 ? 0 : uint16_t(lo(0) >> n));
    setw(1, n > 15 ? 0 : uint16_t(lo(1) >> n));
  }
  if (RW(-62) == 0) {
    setw(0, lo(0) + 4);
    setw(1, lo(1) + 4);
  }
  WW(-44, lo(0));
  WW(-42, lo(1));
  if (RW(-62) == 0) {                                 // far pass: $3f3c6
    D(5) = 10;
    setw(3, uint16_t(lo(3) >> 10));
    setw(4, uint16_t(lo(4) >> 10));
    setw(3, lo(3) & 3);
    setw(4, lo(4) & 3);
    setw(3, lo(3) - 3);
    negw(3);
    setw(4, lo(4) - 3);
    negw(4);
    setw(4, lo(4) + lo(4));
    setw(4, lo(4) + lo(4));
    setw(4, lo(4) + lo(3));
    if (!alt_above(20480)) {
      A(0) = 0x3e81a;
      setw(4, lo(4) + lo(4));
      setw(4, lo(4) + lo(4));
    } else {
      A(0) = 0x3e71a;
      setw(4, asl16(lo(4), 4));
    }
    A(0) += uint32_t(ext(lo(4)));
  } else if (alt_above(36864)) {                      // near pass: $3f402
    A(0) = alt_above(65536) ? 0x3e8aa : 0x3e85a;
  } else {
    setb(2, rd8(0x5ab4c));
    extw(2);
    bool own_tile_only = false;
    if (!alt_above(928)) {
      if (alt_above(352)) A(0) = 0x3f1c4;
      else if (alt_above(16)) A(0) = 0x3f204;
      else A(0) = 0x3f244;
      setb(3, rd8(0x5ab48));
      extw(3);
      setw(3, lo(3) + lo(3));
      setw(3, lo(3) + lo(3));
      A(0) += uint32_t(ext(lo(3)));
      setb(4, rd8(A(0)));
      A(0)++;
      if (int8_t(lob(4)) < 0 || lob(4) == lob(2)) {
        own_tile_only = true;
      } else {
        uint8_t b = rd8(A(0));
        A(0)++;
        if (b == lob(2)) {
          own_tile_only = true;
        } else {
          b = rd8(A(0));
          A(0)++;
          own_tile_only = b == lob(2) || rd8(A(0)) == lob(2);
        }
      }
      if (own_tile_only) {                            // $3f480
        A(0) = 0x3f490;
        wr8(0x5ab95, 1);
      }
    }
    if (!own_tile_only) {                             // $3f492
      A(0) = 0x3efc4;
      A(1) = 0x3ecc4;
      A(2) = 0x3e8c4;
      setb(3, rd8(0x5ab48));
      extw(3);
      setw(3, asl16(lo(3), 4));
      setw(4, lo(3));
      setw(3, lo(3) + lo(3));
      A(0) += uint32_t(ext(lo(3)));
      setw(4, lo(4) + lo(3));
      A(1) += uint32_t(ext(lo(4)));
      setw(3, lo(3) + lo(3));
      A(2) += uint32_t(ext(lo(3)));
      if (alt_above(18432)) {
        A(0) = A(2);
        setw(2, asl16(lo(2), 3));
        A(0) += uint32_t(ext(lo(2)));
      } else if (alt_above(12288)) {
        setw(3, lo(2));
        setw(2, lo(2) + lo(2));
        setw(2, lo(2) + lo(2));
        setw(2, lo(2) + lo(3));
        setw(2, lo(2) + lo(3));
        A(0) = A(1);
        A(0) += uint32_t(ext(lo(2)));
      } else {
        setw(2, lo(2) + lo(2));
        setw(2, lo(2) + lo(2));
        A(0) += uint32_t(ext(lo(2)));
      }
    }
  }
  for (;;) {                                          // $3f4e8: pattern entries
    if (RW(-68) != 0) return;
    setb(2, rd8(A(0)));
    A(0)++;
    extw(2);
    if (lob(2) == 0xFF) return;
    if (int8_t(lob(2)) < 0) {                         // last entry of the list
      WW(-68, RW(-68) + 1);
      setw(2, lo(2) & 127);
    }
    WW(-34, 0);
    WW(-32, 0);
    WB(-36, 0);
    if (RW(-62) != 0 && rd8(0x5aaa5) == 0) {
      if (lo(2) == 4) {                               // the camera's own tile
        WB(-36, rd8(0x5ab94));
        if (!alt_above(1024)) WW(-32, 2);
        else if (!alt_above(3200)) WW(-32, 1);
      } else if (!alt_above(3200)) {
        WW(-32, 1);
        WW(-34, 1);
      }
    }
    A(2) = 0x3e6e8;                                   // $3f568: tile delta
    setw(2, lo(2) + lo(2));
    A(2) += uint32_t(ext(lo(2)));
    A(1) = A(2);
    setb(3, rd8(A(1)));
    A(1)++;
    extw(3);
    setw(0, RW(-44));
    if (addw_neg(0, lo(3))) continue;
    if (int16_t(lo(0)) > int16_t(RW(-48))) continue;
    setb(3, rd8(A(1)));
    extw(3);
    setw(1, RW(-42));
    if (addw_neg(1, lo(3))) continue;
    if (int16_t(lo(1)) > int16_t(RW(-46))) continue;
    A(1) = RL(-52);
    setw(0, lo(0) + lo(0));
    setw(1, asl16(lo(1), RW(-62) == 0 ? 4 : 6));
    setw(0, lo(0) + lo(1));
    A(3) = A(1);
    setw(0, rd16(A(1) + uint32_t(ext(lo(0)))));
    if (int16_t(lo(0)) <= 0) {                        // $3f5ce
      wr16(0x5ac96, 64);
      call_c(0x2f216);
      continue;
    }
    A(3) += uint32_t(ext(lo(0)));
    if (sw(A(3)) < 0 && rd8(0x5aaa8) == 0) continue;
    D(0) = rd32(A(4));                                // $3f604: tile origin - camera
    D(1) = rd32(A(4) + 8);
    if (RW(-62) == 0) {
      D(0) &= 0x0FFFFFFF;
      D(1) &= 0x0FFFFFFF;
    } else {
      D(0) &= 0x00FFFFFF;
      D(1) &= 0x00FFFFFF;
    }
    negl(0);
    negl(1);
    setb(3, rd8(A(2)));
    A(2)++;
    extw(3);
    setb(4, rd8(A(2)));
    extw(4);
    setw(3, asl16(lo(3), 8));
    setw(4, asl16(lo(4), 8));
    swapw(3);
    swapw(4);
    setw(3, 0);
    setw(4, 0);
    if (RW(-62) == 0) {
      D(3) = uint32_t(asl32(int32_t(D(3)), 4));
      D(4) = uint32_t(asl32(int32_t(D(4)), 4));
    }
    D(0) += D(3);
    D(1) += D(4);
    if (mach().full_detail) {                         // remaster: never the coarse mesh
      setw(7, 0);
    } else if (RB(-36) != 0) {                        // coarse mesh: $3f652
      setw(7, 1);
    } else if (RW(-34) == 0) {
      setw(7, 0);
    } else {
      D(6) = 0;
      D(3) = RL(-40);
      D(3) = uint32_t(asr32(int32_t(D(3)), 8));
      if (int16_t(lo(3)) > 17) setw(3, 17);
      A(2) = 0x3f5e0;
      setw(3, lo(3) + lo(3));
      setw(6, rd16(A(2) + uint32_t(ext(lo(3)))));
      if (rd8(0x5aad5) == 0) {
        scale_divisor();
        D(6) = uint32_t(lo(6)) * lo(7);
        D(6) = uint32_t(asr32(int32_t(D(6)), 8));
      }
      swapw(6);
      D(7) = D(0) + 0x800000;
      if (int32_t(D(7)) < 0) negl(7);
      if (!(int32_t(D(7)) > int32_t(D(6)))) {         // else: d7 keeps |x| (tstw below)
        D(7) = D(1) + 0x800000;
        if (int32_t(D(7)) < 0) negl(7);
        setw(7, int32_t(D(7)) <= int32_t(D(6)) ? 0 : 1);
      }
    }
    if (RW(-62) != 0) {                               // $3f6cc
      swapw(0);
      swapw(1);
      D(0) = rol32(D(0), 4);
      D(1) = rol32(D(1), 4);
    } else {
      swapw(0);
      swapw(1);
    }
    swapw(0);
    setw(0, lo(1));
    c.a[7] -= 8;                                      // moveml a0/a4,-(sp)
    wr32(c.a[7], A(0));
    wr32(c.a[7] + 4, A(4));
    D(5) = rd32(A(3));
    A(3) += 4;
    if (int32_t(D(5)) >= 0) {
      D(2) = rd32(A(4) + 4);                          // camera height term
      if (RW(-62) != 0) {
        D(2) += 4096;
        swapw(2);
        D(2) = rol32(D(2), 4);
      } else {
        swapw(2);
      }
      negw(2);
      A(0) = 0x5aed2;
      setw(1, lo(2));
      setw(3, lo(2));
      D(1) = muls(lo(1), sw(A(0)));
      D(1) = uint32_t(asr32(int32_t(D(1)), 8));
      D(2) = muls(lo(2), sw(A(0) + 6));
      D(2) = uint32_t(asr32(int32_t(D(2)), 8));
      D(3) = muls(lo(3), sw(A(0) + 12));
      D(3) = uint32_t(asr32(int32_t(D(3)), 8));
      WW(-8, lo(1));
      WW(-6, lo(2));
      WW(-4, lo(3));
      if (lo(7) != 0) A(3) = D(5);
      for (;;) {                                      // $3f72e: polygons of the tile
        setw(1, rd16(A(3)));
        A(3) += 2;
        if (int16_t(lo(1)) <= 0) {
          if (lo(1) == 0xFFFF) break;
          setw(1, lo(1) & 0x7FFF);                    // altitude threshold
          setw(1, lo(1) + lo(1));
          setw(1, lo(1) + lo(1));
          extl(1);
          if (int32_t(D(1)) > int32_t(RL(-40)) && !mach().full_detail) break;
          setw(1, rd16(A(3)));
          A(3) += 2;
        }
        if (int16_t(lo(1)) > 18) {                    // $3f754
          wr16(0x5ac96, 32);
          call_c(0x2f216);
          break;
        }
        A(5) = 0x61286;
        wr16(A(5), lo(1));
        A(5) += 2;
        A(0) = 0x5aed0;
        D(6) = uint32_t(ext(RW(-8)));                 // movemw fp@(-8),d6/a2/a4
        A(2) = uint32_t(ext(RW(-6)));
        A(4) = uint32_t(ext(RW(-4)));
        setw(3, RW(-32));
        do {                                          // $3f77e: one vertex
          D(2) = D(0);
          swapw(2);
          setw(4, lo(0));
          setw(2, lo(2) + rd16(A(3)));
          A(3) += 2;
          setw(4, lo(4) + rd16(A(3)));
          A(3) += 2;
          A(1) = A(0);
          setw(5, lo(2));
          setw(7, lo(4));
          D(5) = muls(lo(5), sw(A(1)));
          A(1) += 4;
          D(7) = muls(lo(7), sw(A(1)));
          A(1) += 2;
          D(7) += D(5);
          D(7) = uint32_t(asr32(int32_t(D(7)), 8));
          setw(7, lo(7) + lo(6));
          setw(7, asl16(lo(7), D(3)));
          wr16(A(5), lo(7));
          A(5) += 2;
          setw(5, lo(2));
          setw(7, lo(4));
          D(5) = muls(lo(5), sw(A(1)));
          A(1) += 4;
          D(7) = muls(lo(7), sw(A(1)));
          A(1) += 2;
          D(7) += D(5);
          D(7) = uint32_t(asr32(int32_t(D(7)), 8));
          setw(7, lo(7) + uint16_t(A(2)));
          setw(7, asl16(lo(7), D(3)));
          wr16(A(5), lo(7));
          A(5) += 2;
          D(2) = muls(lo(2), sw(A(1)));
          A(1) += 2;
          D(4) = muls(lo(4), sw(A(1) + 2));
          D(4) += D(2);
          D(4) = uint32_t(asr32(int32_t(D(4)), 8));
          setw(4, lo(4) + uint16_t(A(4)));
          setw(4, asl16(lo(4), D(3)));
          wr16(A(5), lo(4));
          A(5) += 2;
          setw(1, lo(1) - 1);
        } while (int16_t(lo(1)) > 0);
        c.a[7] -= 8;                                  // moveml d0/a3,-(sp)
        wr32(c.a[7], D(0));
        wr32(c.a[7] + 4, A(3));
        call_c(0x38c0c);
        D(0) = rd32(c.a[7]);
        A(3) = rd32(c.a[7] + 4);
        c.a[7] += 8;
      }
    }
    A(0) = rd32(c.a[7]);                              // $3f7d8: moveml (sp)+,a0/a4
    A(4) = rd32(c.a[7] + 4);
    c.a[7] += 8;
  }
}

// ---- $3f284 ------------------------------------------------------------------------
void terrain_3f284() {
  m68k::Cpu& c = cpu();
  c.push32(c.a[6]);                                   // link a6,#-70
  c.a[6] = c.a[7];
  c.a[7] -= 70;
  wr8(0x5ab95, 0);
  wr16(0x61284, 0);
  if (rd8(0x5aaa8) == 0) {                            // set plane 1 / colour $5ac4c
    setw(0, 2);
    setw(1, 2);
  } else {
    setw(0, 15);
    setw(1, 0xFFFF);
  }
  setw(2, 0);
  setw(3, 0);
  wr16(0x5a9de, lo(0));
  wr16(0x5a9e0, lo(1));
  wr16(0x5a9e2, lo(2));
  wr16(0x5a9e4, lo(3));
  D(0) = rd32(0x5ad70);                               // scaled altitude
  negl(0);
  if (rd8(0x5aad5) == 0 && int32_t(D(0)) <= 524272) {
    D(0) = uint32_t(asr32(int32_t(D(0)), 4));
    scale_divisor();
    D(0) = uint32_t(lo(0)) * lo(7);
    D(0) = uint32_t(asr32(int32_t(D(0)), 4));
  }
  WL(-40, D(0));
  if (int32_t(D(0)) > 1016) {                         // far pass ($3f342)
    wr16(0x5ac4c, 6);
    c.push32(0x3f30c);
    WW(-62, 0);
    WW(-68, 0);
    WL(-52, 0x57fa4);
    WW(-64, 12);
    WW(-48, 7);
    WW(-46, 7);
    terrain_pass();
    c.a[7] += 4;
  }
  wr16(0x5ac4c, 6);                                   // near pass ($3f31c)
  c.push32(0x3f318);
  WW(-62, 1);
  WW(-68, 0);
  WL(-52, 0x58168);
  WW(-64, 8);
  WW(-48, 31);
  WW(-46, 31);
  terrain_pass();
  c.a[7] += 4;
  c.a[7] = c.a[6];                                    // unlk a6
  c.a[6] = c.pop32();
}

}  // namespace

// Draws through $38c0c (blitter): not verifiable per call.
NATIVE_ASM(0x3f284, terrain_3f284, false);

}  // namespace native
