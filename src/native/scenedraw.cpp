// Top of the 3D scene drawing path (called from game_frame $2112c):
//   $2f220  camera position update from the scenario record ($5ad5a..)
//   $2fc14  draw the object lists: the scenario records first ($63c7a),
//           then the sorted list at $649ba, dispatching each object to its
//           draw routine from the type table at $36094 (jsr a2@)
//   $2fffc/$3000e  helpers: record position -> $5ae28/$5ae34
//   $2f364  (not used here)
// Register-convention assembly on the live register file.
#include "amiga/machine.hpp"
#include "native/asmregs.hpp"
#include "native/orig.hpp"
#include "native/remaster.hpp"
#include "native/scene_frame.hpp"
#include "native/vars.hpp"

namespace native {
using namespace asmr;

namespace {

int16_t sw(uint32_t a) { return int16_t(rd16(a)); }
int32_t sl(uint32_t a) { return int32_t(rd32(a)); }
int32_t e16(uint16_t v) { return int16_t(v); }
void negl(int r) { D(r) = uint32_t(-int32_t(D(r))); }
constexpr uint32_t scenario_base = 0x5b47c;

void capture_object(uint32_t obj_base, scene::ObjectList list, uint16_t list_word = 0,
                    uint32_t list_entry = 0) {
  if (!scene::enabled()) return;
  scene::ObjectDraw o;
  o.obj_base = obj_base;
  o.list_entry = list_entry;
  o.draw_fn = A(2);
  o.aux0 = rd32(0x5ad2e);
  o.aux1 = rd32(0x5ad32);
  o.type_index = rd16(0x5acac);
  o.mesh_id = o.type_index;  // primary mesh identity = type table key
  o.list_word = list_word;
  o.list = list;
  o.eye_x = int16_t(rd16(0x5ae22));
  o.eye_y = int16_t(rd16(0x5ae24));
  o.eye_z = int16_t(rd16(0x5ae26));
  o.pos_x = int32_t(rd32(0x5ae28));
  o.pos_y = int32_t(rd32(0x5ae2c));
  o.pos_z = int32_t(rd32(0x5ae30));
  // Real camera-space distance from hypot $308cc (written just before dispatch).
  o.distance = int16_t(rd16(0x5ae38));
  scene::note_object(o);
}


// ---- $2f220 ---------------------------------------------------------------
void camera_position_2f220() {
  uint32_t sd[7];
  for (int i = 0; i < 7; ++i) sd[i] = D(i);          // moveml d0-d6,-(sp)
  auto restore = [&] { for (int i = 0; i < 7; ++i) D(i) = sd[i]; };
  auto store_result = [&] {
    wr32(0x5ad5a, D(2));
    wr32(0x5ad5e, D(3));
    wr32(0x5ad62, D(4));
    restore();
  };
  if (rd8(0x5aaa4) == 0) {                            // $2f298
    D(2) = D(3) = D(4) = 0;
    goto L2f334;
  }
  {
    setw(0, 0);
    A(0) = scenario_base + uint32_t(e16(rd16(0x5abd6)));
    D(2) = rd32(A(0) + 20) & 0x3FFFFF;
    D(2) += rd32(0x5af2a);
    if (int32_t(D(2)) < 0) { negl(2); D(0) |= 1; }
    D(2) = uint32_t(asr32(int32_t(D(2)), 8));
    D(3) = rd32(A(0) + 24);
    D(3) += rd32(0x5af2e);
    if (int32_t(D(3)) < 0) { negl(3); D(0) |= 2; }
    D(3) = uint32_t(asr32(int32_t(D(3)), 8));
    D(4) = rd32(A(0) + 28) & 0x3FFFFF;
    D(4) += rd32(0x5af32);
    if (int32_t(D(4)) < 0) { negl(4); D(0) |= 4; }
    D(4) = uint32_t(asr32(int32_t(D(4)), 8));
    uint16_t w2 = lo(2), w3 = lo(3), w4 = lo(4);
    call_c(0x308cc);
    D(2) = uint32_t(e16(w2));                         // movemw (sp)+,d2-d4
    D(3) = uint32_t(e16(w3));
    D(4) = uint32_t(e16(w4));
    setw(5, lo(1));
    setw(1, lo(1) - 512);
    if (!(int16_t(lo(1)) > 0 && int32_t(int16_t(lo(5))) - 512 > 0)) {
      D(2) = D(3) = D(4) = 0;
      goto L2f334;
    }
    setw(6, lo(5));
    setw(6, asr16(lo(6), 1));
    if (int16_t(lo(1)) < int16_t(lo(6))) {            // $2f310
      extl(1);
      D(1) = uint32_t(asl32(int32_t(D(1)), 8));
      divuw(D(1), lo(5));
      D(2) = uint32_t(lo(2)) * lo(1);
      D(3) = uint32_t(lo(3)) * lo(1);
      D(4) = uint32_t(lo(4)) * lo(1);
      if (!(D(0) & 1)) negl(2);
      if (!(D(0) & 2)) negl(3);
      if (!(D(0) & 4)) negl(4);
      goto L2f334;
    }
    D(6) = 0;
    setw(6, lo(5));
    setw(6, lo(6) - lo(1));
    extl(6);
    swapw(6);
    divuw(D(6), lo(5));
    D(2) = uint32_t(lo(2)) * lo(6);
    D(3) = uint32_t(lo(3)) * lo(6);
    D(4) = uint32_t(lo(4)) * lo(6);
    for (int r = 2; r <= 4; ++r) {
      bool carry = (D(r) >> 7) & 1;
      D(r) = uint32_t(asr32(int32_t(D(r)), 8));
      if (carry) D(r) += 1;
    }
    if (D(0) & 1) negl(2);
    if (D(0) & 2) negl(3);
    if (D(0) & 4) negl(4);
    A(0) = scenario_base + uint32_t(e16(rd16(0x5abd6)));
    D(0) = rd32(A(0) + 20) & 0x3FFFFF;
    D(2) -= D(0);
    D(3) -= rd32(A(0) + 24);
    D(0) = rd32(A(0) + 28) & 0x3FFFFF;
    D(4) -= D(0);
    store_result();
    return;
  }
L2f334:
  D(0) = rd32(0x5af2a);
  D(1) = rd32(0x5af2e);
  D(5) = rd32(0x5af32);
  D(2) += D(0);
  D(3) += D(1);
  D(4) += D(5);
  store_result();
}
NATIVE_ASM(0x2f220, camera_position_2f220, true);

// ---- $2fffc / $3000e ------------------------------------------------------------
void record_pos_tail() {                              // $3001e
  A(2) += uint32_t(e16(lo(1)));
  setw(6, lo(6) & 15);
  D(3) = rd32(A(2) + 20) & 0xFFFFF;
  D(3) = uint32_t(asr32(int32_t(D(3)), D(6)));
  D(2) = uint32_t(asl32(int32_t(D(2)), 8));
  D(2) += D(3);
  D(3) = rd32(A(2) + 28) & 0xFFFFF;
  D(3) = uint32_t(asr32(int32_t(D(3)), D(6)));
  D(4) = uint32_t(asl32(int32_t(D(4)), 8));
  D(4) += D(3);
  D(3) = rd32(A(2) + 24);
  D(1) = D(3);
  D(3) = uint32_t(asr32(int32_t(D(3)), D(6)));
  D(1) += rd32(0x5ad5e);
  D(1) = uint32_t(asr32(int32_t(D(1)), D(6)));
  wr32(0x5ae34, D(1));
  wr8(0x5abb3, 1);
}
void h2fffc() {
  A(2) = 0x5d47c;
  setw(1, lo(7));
  setw(6, lo(7));
  setw(1, lo(1) & 0xFF00);
  setw(1, asr16(lo(1), 3));
  record_pos_tail();
}
void h3000e() {
  A(2) = scenario_base;
  setw(1, lo(7));
  setw(6, lo(7));
  setw(1, lo(1) & 0xFF00);
  setw(1, lo(1) + lo(1));
  record_pos_tail();
}

// ---- $2fc14 ---------------------------------------------------------------
void draw_scene_body() {
  wr8(0x5ab5c, 1);
  wr8(0x5ab53, 4);
  wr8(0x5abb5, 0);
  wr16(0x5adb2, 0);
  wr32(0x5ac2a, 0);
  wr16(0x5aca2, 0);
  for (;;) {                                          // $2fc3c: scenario records
    A(0) = 0x63c7a + uint32_t(e16(rd16(0x5aca2)));
    setw(0, rd16(A(0)));
    if (int16_t(lo(0)) <= 0) break;
    A(0) = scenario_base;
    wr16(0x5acac, lo(0));
    setw(0, uint16_t(lo(0) << 8));
    setw(0, lo(0) + lo(0));
    wr16(0x5acae, lo(0));
    A(0) += uint32_t(e16(lo(0)));
    D(7) = 0;
    setw(7, rd16(A(0) + 6));
    setw(7, lo(7) - rd16(0x5ac44));
    swapw(7);
    D(7) = uint32_t(asl32(int32_t(D(7)), 6));
    D(4) = rd32(A(0) + 20) & 0x3FFFFF;
    D(7) += D(4);
    D(6) = 0;
    setw(6, rd16(A(0) + 8));
    setw(6, lo(6) - rd16(0x5ac46));
    swapw(6);
    D(6) = uint32_t(asl32(int32_t(D(6)), 6));
    D(4) = rd32(A(0) + 28) & 0x3FFFFF;
    D(6) += D(4);
    D(2) = D(7);
    D(4) = D(6);
    D(7) = uint32_t(asr32(int32_t(D(7)), 8));
    D(6) = uint32_t(asr32(int32_t(D(6)), 8));
    setw(0, rd16(0x5ad6a));
    extl(0);
    D(7) += D(0);
    if (int32_t(D(7)) < 0) D(7) = uint32_t(-int32_t(D(7)));
    D(7) = uint32_t(asr32(int32_t(D(7)), 8));
    D(7) = uint32_t(asr32(int32_t(D(7)), 4));
    setw(0, rd16(0x5ad6e));
    extl(0);
    D(6) += D(0);
    if (int32_t(D(6)) < 0) D(6) = uint32_t(-int32_t(D(6)));
    D(6) = uint32_t(asr32(int32_t(D(6)), 8));
    D(6) = uint32_t(asr32(int32_t(D(6)), 4));
    D(0) = rd32(A(0) + 16);
    D(0) += rd32(0x5ad70);
    if (int32_t(D(0)) < 0) D(0) = uint32_t(-int32_t(D(0)));
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
      wr16(0x5ac96, 41);
      call_c(0x2f216);
      setw(7, 239);
    }
    A(1) = 0x30e9a;
    setb(6, rd8(A(1) + uint32_t(e16(lo(7)))));
    extw(6);
    wr16(0x5adb0, lo(6));
    D(3) = rd32(A(0) + 24);
    D(0) = D(3);
    D(0) += rd32(0x5ad5e);
    D(0) = uint32_t(asr32(int32_t(D(0)), D(6)));
    wr32(0x5ae34, D(0));
    wr8(0x5abb3, 1);
    D(2) = uint32_t(asr32(int32_t(D(2)), D(6)));
    D(3) = uint32_t(asr32(int32_t(D(3)), D(6)));
    D(4) = uint32_t(asr32(int32_t(D(4)), D(6)));
    wr32(0x5ae28, D(2));
    wr32(0x5ae2c, D(3));
    wr32(0x5ae30, D(4));
    D(2) = uint32_t(asr32(int32_t(D(2)), 8));
    D(3) = uint32_t(asr32(int32_t(D(3)), 8));
    D(4) = uint32_t(asr32(int32_t(D(4)), 8));
    wr16(0x5ae22, lo(2));
    wr16(0x5ae24, lo(3));
    wr16(0x5ae26, lo(4));
    call_c(0x30872);
    {
      uint32_t obj_base = A(0);
      A(1) = 0x36094;
      setw(1, rd16(0x5acac));
      setw(1, lo(1) + lo(1));
      setw(1, lo(1) + lo(1));
      setw(7, lo(1));
      setw(1, lo(1) + lo(1));
      setw(1, lo(1) + lo(1));
      setw(1, lo(1) + lo(7));
      A(1) += uint32_t(e16(lo(1)));
      A(2) = rd32(A(1));
      A(1) += 4;
      A(0) = rd32(A(1));
      A(1) += 4;
      wr32(0x5ad2e, rd32(A(1)));
      A(1) += 4;
      wr32(0x5ad32, rd32(A(1)));
      A(1) += 4;
      capture_object(obj_base, scene::ObjectList::Scenario);
    }
    call_c(A(2));
    wr16(0x5aca2, rd16(0x5aca2) + 2);
  }
  wr16(0x5aca2, rd16(0x5aca8));                       // $2fd90
  for (;;) {                                          // $2fd9a: sorted object list
    A(0) = 0x649ba;
    setw(0, rd16(0x5aca2));
    A(0) += uint32_t(e16(lo(0)));
    setw(7, rd16(A(0)));
    A(0) += 2;
    if (lo(7) == 0xFFFF) goto L2ff2e;
    wr8(0x5ab53, lob(7));
    setb(0, lob(7));
    setw(0, lo(0) & 15);
    wr16(0x5adb0, lo(0));
    uint32_t sorted_entry = A(0) - 2;  // start of 24-byte slot (word + ptr + ...)
    A(1) = rd32(A(0));
    A(0) += 4;
    uint32_t sorted_obj = A(1);
    if (int16_t(rd16(A(1))) < 0) goto L2ff16;
    if (rd32(A(1)) == 203928) {
      if (sl(0x5ad5e) < -4194304) goto L2ff16;
    }
    for (int i = 0; i < 3; ++i) D(2 + i) = uint32_t(e16(rd16(A(0) + 2u * uint32_t(i))));
    A(0) += 6;
    wr32(0x5ac2a, rd32(A(0)));
    setw(1, lo(7));
    setw(1, lo(1) >> 8);
    wr16(0x5acac, lo(1));
    wr8(0x5abb3, 0);
    if (lo(7) & 0x40) {
      h2fffc();
      goto L2fe24;
    }
    if (lo(7) & 0x10) {
      h3000e();
      goto L2fe24;
    }
    wr16(0x5ae22, lo(2));
    wr16(0x5ae24, lo(3));
    wr16(0x5ae26, lo(4));
    D(2) = uint32_t(asl32(int32_t(D(2)), 8));
    D(3) = uint32_t(asl32(int32_t(D(3)), 8));
    D(4) = uint32_t(asl32(int32_t(D(4)), 8));
    wr32(0x5ae28, D(2));
    wr32(0x5ae2c, D(3));
    wr32(0x5ae30, D(4));
    goto L2fe44;
  L2fe24:
    wr32(0x5ae28, D(2));
    wr32(0x5ae2c, D(3));
    wr32(0x5ae30, D(4));
    D(2) = uint32_t(asr32(int32_t(D(2)), 8));
    D(3) = uint32_t(asr32(int32_t(D(3)), 8));
    D(4) = uint32_t(asr32(int32_t(D(4)), 8));
    wr16(0x5ae22, lo(2));
    wr16(0x5ae24, lo(3));
    wr16(0x5ae26, lo(4));
  L2fe44:
    setw(1, rd16(A(0) + 4));
    extl(1);
    D(1) = uint32_t(asl32(int32_t(D(1)), D(0)));
    if (int32_t(D(1)) >= 1024) goto L2fe8e;
    if (int32_t(D(1)) >= 256) {
      setw(0, rd16(0x5abd2));
      setw(1, lo(7));
      if (!(lo(1) & 0x100)) {
        setw(0, lo(0) & 3);
        if (lo(0) != 0) goto L2fe8e;
      } else {
        setw(0, lo(0) & 3);
        if (lo(0) != 2) goto L2fe8e;
      }
    }
    for (int i = 0; i < 3; ++i) D(2 + i) = uint32_t(e16(rd16(0x5ae22 + 2u * uint32_t(i))));
    call_c(0x30872);
    wr16(A(0) + 4, lo(1));
  L2fe8e:
    if (!(lo(7) & 0x40) && !(lo(7) & 0x10)) {
      uint16_t v = rd16(A(0) + 8);
      wr16(0x5adb2, v);
      if (int16_t(v) < 0) {
        int r = int(int8_t(rd8(A(0) + 6))) - 1;
        wr8(A(0) + 6, uint32_t(r));
        if (r >= 0) goto L2ff16;
        wr8(A(0) + 6, rd8(0x5abb4));
      }
    }
    {                                                 // $2feb2
      wr8(A(0) + 7, rd8(A(0) + 7) - 1);
      wr8(0x5abb5, rd8(A(0) + 7));
      setw(1, rd16(A(0) + 4));
      wr16(0x5ae38, lo(1));
      setw(7, lo(7) & 0xFF00);
      setw(7, lo(7) + lo(7));
      wr16(0x5acae, lo(7));
      if (lo(7) == rd16(0x5abd6)) {
        if (rd8(0x5aa7d) == 0) setw(1, 32767);
        wr16(0x5ae3a, lo(1));
      }
    }
    A(2) = rd32(A(1));
    A(1) += 4;
    A(0) = rd32(A(1));
    A(1) += 4;
    wr32(0x5ad2e, rd32(A(1)));
    A(1) += 4;
    wr32(0x5ad32, rd32(A(1)));
    A(1) += 4;
    capture_object(sorted_obj, scene::ObjectList::Sorted, lo(7), sorted_entry);
    call_c(A(2));
    A(0) = 0x649ba;
    setw(1, rd16(0x5aca2));
    if (!(int16_t(lo(0)) > 0)) D(0) = 0xFFFFFFFFu;
    wr16(A(0) + 20 + uint32_t(e16(lo(1))), lo(0));
  L2ff16:
    wr16(0x5aca2, rd16(0x5aca2) + 24);
  }
L2ff2e:
  wr8(0x5ab2d, 0);
  wr8(0x5ab30, 0);
  if (rd8(0x5aa7d) != 0) return;
  if (int8_t(rd8(0x5ab2f)) > 1) {
    wr8(0x5ab30, rd8(0x5ab30) + 1);
  } else {
    setw(0, rd16(0x5abd2));
    setw(1, lo(0));
    setw(1, lo(1) & 2);
    if (lo(1) == 0) wr8(0x5ab30, rd8(0x5ab30) + 1);
    setw(0, lo(0) & 3);
    if (lo(0) != 0) return;
  }
  wr16(0x5aca2, 0);
  A(1) = scenario_base;
  D(0) = 0;
  do {                                                // $2ff80
    setw(1, rd16(0x5abd6));
    if (lo(0) != lo(1)) {
      uint32_t o0 = uint32_t(e16(lo(0))), o1 = uint32_t(e16(lo(1)));
      if (rd8(A(1) + 1 + o0) & 0x40) {
        D(2) = rd32(A(1) + 20 + o0);
        D(3) = rd32(A(1) + 24 + o0);
        D(4) = rd32(A(1) + 28 + o0);
        D(2) -= rd32(A(1) + 20 + o1);
        D(3) -= rd32(A(1) + 24 + o1);
        D(4) -= rd32(A(1) + 28 + o1);
        D(5) = D(2);
        if (int32_t(D(5)) < 0) negl(5);
        D(6) = D(3);
        if (int32_t(D(6)) < 0) negl(6);
        D(7) = D(4);
        if (int32_t(D(7)) < 0) negl(7);
        if (int32_t(D(6)) < int32_t(D(5))) {
          if (!(int32_t(D(7)) > int32_t(D(5)))) D(7) = D(5);
        } else if (!(int32_t(D(7)) > int32_t(D(6)))) {
          D(7) = D(6);
        }
        D(1) = 0;
        while (int32_t(D(7)) > 8388607) {
          setw(1, lo(1) + 2);
          D(7) = uint32_t(asr32(int32_t(D(7)), 2));
        }
        setw(5, lo(1));
        setw(5, lo(5) + 8);
        D(2) = uint32_t(asr32(int32_t(D(2)), D(5)));
        D(3) = uint32_t(asr32(int32_t(D(3)), D(5)));
        D(4) = uint32_t(asr32(int32_t(D(4)), D(5)));
        setw(7, lo(0));
        setw(7, asr16(lo(7), 1));
        setb(7, lob(7) | 0x10);
        call_c(0x39de2);
      }
    }
    setw(0, lo(0) + 512);
  } while (int16_t(lo(0)) < 8192);
}
void draw_scene_2fc14() {
  if (remaster::scene_hooks) remaster::scene_begin();
  if (scene::enabled()) {
    uint64_t fid = g_machine ? g_machine->frame_count() : 0;
    scene::begin_frame(fid);
    scene::note_camera();
  }
  draw_scene_body();
  if (scene::enabled()) scene::end_frame();
  if (remaster::scene_hooks) remaster::scene_end();
}
NATIVE_ASM(0x2fc14, draw_scene_2fc14, true);

}  // namespace
}  // namespace native
