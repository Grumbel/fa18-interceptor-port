// Sky quad of the 3D view ($42f62 / $42f6a, body $42f70): a quad at the
// camera's height above it (corners $42f38) is transformed by the view
// matrix ($5aed0, or the attitude matrix $5aee2 for the cockpit's second
// pass), clipped by $43364 (still original) and, from the clip result at
// $63b44, rebuilt as a screen polygon at $60684 (count + x/y words) that
// covers the part of the view above the horizon.  d0 = 0 and the plane
// operation "set plane 0" on success, d0 = 1 when nothing is visible.
// See docs/ground_sea_sky.md.  Register-convention assembly on the live
// register file with the frame of the original (`link a6,#-2`).
#include "native/asmregs.hpp"
#include "native/orig.hpp"
#include "native/remaster.hpp"
#include "native/vars.hpp"

namespace native {
using namespace asmr;

namespace {

int16_t sw(uint32_t a) { return int16_t(rd16(a)); }
uint32_t muls(uint16_t a, int16_t b) { return uint32_t(int32_t(int16_t(a)) * b); }
void put(uint16_t v) {                     // movew ...,(a1)+
  wr16(A(1), v);
  A(1) += 2;
}

// asrl #8,dN; bccs; addqw #1,dN: round by the last bit shifted out
void asr8_round(int n) {
  bool carry = (D(n) >> 7) & 1;
  D(n) = uint32_t(int32_t(D(n)) >> 8);
  if (carry) setw(n, lo(n) + 1);
}

// ---- helpers $432b8 .. $432fe: corners of the screen polygon -------------------
void edge_points() {                       // $432b8: the two clip points d0 / d1
  setw(0, asl16(lo(0), 3));
  setw(1, asl16(lo(1), 3));
  A(0) = 0x60c84;
  setw(2, 319);
  setw(4, lo(2));
  setw(2, lo(2) - rd16(A(0) + uint32_t(ext(lo(0)))));
  setw(3, 179);
  setw(5, lo(3));
  setw(3, lo(3) - rd16(A(0) + 2 + uint32_t(ext(lo(0)))));
  put(lo(2));
  put(lo(3));
  setw(4, lo(4) - rd16(A(0) + uint32_t(ext(lo(1)))));
  setw(5, lo(5) - rd16(A(0) + 2 + uint32_t(ext(lo(1)))));
  put(lo(4));
  put(lo(5));
}
void top_left() { wr32(A(1), 0); A(1) += 4; }        // $432e8 (0, 0)
void top_right() { put(319); put(0); }               // $432ec
void bottom_right() { put(319); put(179); }          // $432f4
void bottom_left() { put(0); put(179); }             // $432fe
void start(uint16_t n) {
  A(1) = 0x60684;
  put(n);
}
void restart3() {                                    // a second, 3-point polygon
  start(3);
  A(1) += 8;
}
bool sky_flag2() {
  setw(6, rd16(0x5abc2));
  setw(6, lo(6) & 2);
  return lo(6) != 0;
}

void finish(bool ok) {
  m68k::Cpu& c = cpu();
  if (ok) {                                          // $43288
    wr16(0x5a9de, 1);
    wr16(0x5a9e0, 1);
    wr32(0x5a9e2, 0);
    wr8(0x5ab96, 1);
    D(0) = 0;
  } else {                                           // $432ac
    wr8(0x5ab96, 0);
    D(0) = 1;
  }
  flags(D(0), 32);
  c.a[7] = c.a[6];                                   // unlk a6
  c.a[6] = c.pop32();
  remaster::sky_quad_done(ok);
}

// ---- $42f70 --------------------------------------------------------------------
void sky_quad(uint32_t matrix) {
  m68k::Cpu& c = cpu();
  A(2) = matrix;
  A(4) = A(2);
  c.push32(c.a[6]);                                  // link a6,#-2
  c.a[6] = c.a[7];
  c.a[7] -= 2;
  A(1) = 0x42f38;
  A(3) = 0x60684;
  A(0) = c.a[6] - 2;
  wr16(A(0), 4);
  do {                                               // $42f88: one corner
    setw(2, rd16(A(1)));
    A(1) += 2;
    D(3) = rd32(0x5ad5e);
    swapw(3);
    setw(3, asr16(lo(3), 2));
    setw(4, rd16(A(1)));
    A(1) += 2;
    A(2) = A(4);
    for (int row = 0; row < 2; ++row) {
      setw(5, lo(2));
      setw(6, lo(3));
      setw(7, lo(4));
      D(5) = muls(lo(5), sw(A(2)));
      D(6) = muls(lo(6), sw(A(2) + 2));
      D(7) = muls(lo(7), sw(A(2) + 4));
      A(2) += 6;
      D(7) += D(6);
      D(7) += D(5);
      asr8_round(7);
      wr16(A(3), lo(7));
      A(3) += 2;
    }
    D(2) = muls(lo(2), sw(A(2)));
    D(3) = muls(lo(3), sw(A(2) + 2));
    D(4) = muls(lo(4), sw(A(2) + 4));
    A(2) += 6;
    D(4) += D(3);
    D(4) += D(2);
    asr8_round(4);
    wr16(A(3), lo(4));
    A(3) += 2;
    setw(3, lo(4));
    A(3) += 26;
    wr16(A(0), rd16(A(0)) - 1);
  } while (sw(A(0)) > 0);
  A(4) = 0x63b44;                                    // clear the clip result
  for (int i = 0; i < 3; ++i) {
    wr32(A(4), 0);
    A(4) += 4;
  }
  wr32(A(4), 0);
  call_c(0x43364);
  A(1) = 0x63b44;                                    // $42ff8: case tree
  const bool f0 = rd16(A(1)) != 0, f2 = rd16(A(1) + 2) != 0;
  const bool f4 = rd16(A(1) + 4) != 0, f6 = rd16(A(1) + 6) != 0;
  auto pts = [&](uint32_t a, uint32_t b) {
    setw(0, rd16(A(1) + a));
    setw(1, rd16(A(1) + b));
  };
  if (f2) {
    if (f6) {                                        // $4308a
      pts(10, 14);
      start(4);
      bool alt = rd8(0x5aa7d) == 0 && sky_flag2();
      edge_points();
      bottom_right();
      bottom_left();
      if (!alt) {
        A(1) = 0x6068e;
        top_right();
        top_left();
      }
      return finish(true);
    }
    if (f0) {                                        // $430d6
      pts(10, 8);
      start(5);
      bool alt = sky_flag2();
      edge_points();
      top_right();
      bottom_right();
      bottom_left();
      if (!alt) {
        restart3();
        top_left();
      }
      return finish(true);
    }
    if (f4) {                                        // $43124
      pts(10, 12);
      start(5);
      bool alt = sky_flag2();
      edge_points();
      bottom_right();
      top_right();
      top_left();
      if (alt) {
        restart3();
        bottom_left();
      }
      return finish(true);
    }
    return finish(false);
  }
  if (f0) {
    if (f6) {                                        // $43172
      pts(8, 14);
      start(5);
      bool alt = sky_flag2();
      edge_points();
      bottom_right();
      bottom_left();
      top_left();
      if (!alt) {
        restart3();
        top_right();
      }
      return finish(true);
    }
    if (f4) {                                        // $431c0
      pts(8, 12);
      start(4);
      bool low = int16_t(rd16(0x5ad8a)) < 14400;
      edge_points();
      bottom_left();
      top_left();
      if (low) {
        A(1) = 0x6068e;
        bottom_right();
        top_right();
      }
      return finish(true);
    }
    return finish(false);
  }
  if (!f4) {                                         // $43250: whole view
    start(4);
    top_left();
    top_right();
    bottom_right();
    bottom_left();
    setw(0, rd8(0x5aa7d) ? rd16(0x5ad8c) : rd16(0x5ad82));
    return finish(int16_t(lo(0)) > 14400);
  }
  if (f6) {                                          // $43202
    pts(12, 14);
    start(5);
    bool alt = sky_flag2();
    edge_points();
    top_right();
    top_left();
    bottom_left();
    if (alt) {
      restart3();
      bottom_right();
    }
    return finish(true);
  }
  finish(false);
}

void sky_42f62() { sky_quad(0x5aee2); }
void sky_42f6a() { sky_quad(0x5aed0); }

}  // namespace

NATIVE_ASM(0x42f62, sky_42f62, true);
NATIVE_ASM(0x42f6a, sky_42f6a, true);

}  // namespace native
