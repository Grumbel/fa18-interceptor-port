// Mission object setup ($3cf0e): picks the spawn list of the mission
// ($5ab9e), variant ($5adf0) and pilot level (config byte 18 + mission) from
// the table $3dfba and creates the scenario objects with $3d2ea.  After the
// original list, the extra spawns of a world file are created
// (world::extra_spawns, docs/world_format.md).
// Register-convention assembly on the live register file.
#include "native/asmregs.hpp"
#include "native/orig.hpp"
#include "native/vars.hpp"
#include "native/world.hpp"

namespace native {
using namespace asmr;

namespace {

// $3cfc6: d0 = 2 * level of the current mission (0 for missions 126/127)
void level_3cfc6() {
  D(0) = 0;
  setb(1, rd8(0x5ab9e));
  if (lob(1) == 126 || lob(1) == 127) return;
  extw(1);
  A(0) = rd32(0x2d03c);
  setb(0, rd8(A(0) + 18 + uint32_t(ext(lo(1)))));  // a0@($12,d1.w)
  extw(0);
  setw(0, lo(0) + lo(0));
}

void spawn_3d2ea() { call_c(0x3d2ea); }  // creates the object of the entry at a2

void mission_setup_3cf0e() {
  call_c(0x39398);
  wr8(0x5aba1, 0);
  wr8(0x5aba3, 0);
  wr16(0x5aa7a, 0);
  setb(0, rd8(0x5ab9e));
  extw(0);
  if (lob(0) == 125) {                       // $3d0b2: copy of the player
    call_c(0x3d0b2);
    return;
  }
  if (lob(0) == 126 || lob(0) == 127) D(0) = 10;
  else if (int16_t(lo(0)) > 9) return;
  A(0) = 0x3dfba;
  setw(0, lo(0) - 1);
  setw(0, lo(0) + lo(0));
  setw(1, lo(0));
  setw(0, lo(0) + lo(0));
  setw(0, lo(0) + lo(1));
  setw(0, lo(0) + lo(0));
  if (rd8(0x5ab23) != 0) {                   // variant
    setw(1, rd16(0x5ae14));
  } else {
    setw(1, rd16(0x5adf0));
    setw(1, lo(1) & 3);
    if (lo(1) != 0) {
      setw(1, lo(1) - 1);
      wr16(0x5ae14, lo(1));
    }
  }
  setw(1, lo(1) + lo(1));
  setw(1, lo(1) + lo(1));
  setw(0, lo(0) + lo(1));
  A(2) = A(0);
  A(2) += uint32_t(ext(rd16(A(0) + uint32_t(ext(lo(0))))));
  setw(0, rd16(A(0) + 2 + uint32_t(ext(lo(0)))));
  wr8(0x5aba2, lob(0));
  level_3cfc6();
  A(2) += uint32_t(ext(rd16(A(2) + uint32_t(ext(lo(0))))));
  wr32(0x5aa32, A(2));
  for (;;) {                                 // $3cfa6: the list
    spawn_3d2ea();
    for (;;) {
      A(2) += 10;
      if (int16_t(rd16(A(2))) < 0) {
        world::spawn_extras();
        return;
      }
      setw(0, rd16(A(2) + 8));
      setw(0, lo(0) & 127);
      if (int8_t(lob(0)) <= int8_t(rd8(0x5ab9f))) break;
    }
  }
}

}  // namespace

// creates objects (and random positions); checked by the equivalence run
NATIVE_ASM(0x3cf0e, mission_setup_3cf0e, false);

}  // namespace native
