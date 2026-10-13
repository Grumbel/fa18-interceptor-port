// Cockpit command decoder, view/mode toggles and the altitude/pitch alert
// state. Original: hunk 0 $23d54-$24552 (compiled C).
#include "native/globals.hpp"
#include "native/orig.hpp"
#include "native/vars.hpp"

namespace native {
using namespace g;
using namespace v;

namespace {
// The original compiled code leaves values in d0 that assembly callees (the
// key-queue tail at $2ef98) consume; set the low word like the original.
inline void set_d0_word(uint16_t v) { cpu().d[0] = (cpu().d[0] & 0xFFFF0000u) | v; }
constexpr uint32_t scenario_table = 0x5b47c;          // 512-byte records per scenario
inline uint32_t scenario_rec(int16_t idx) { return scenario_table + (uint32_t(int32_t(idx)) << 9); }
}  // namespace

// $23d54: turn the pending key/stick event flags into the current
// "cockpit command" word ($5add8) and the table-driven timing/flags that go
// with it. Command word: bit15 = momentary (key), bit14 = toggled, bit12 =
// uses channel 4 instead of 2, low byte = index into the 28-byte record
// table at $521d0.
static void decode_cockpit_command() {
  uint16_t cmd = w_5add8;
  uint32_t flags_word = rd32(0x5ae48);   // bytes 5ae48..5ae4b (fp-12 in the original)
  uint8_t lo = uint8_t(flags_word);
  if (w_5abc4 & 1) {
    if (w_5add8 == 0) {
      w_5abc4 = uint16_t(w_5abc4 & ~1);
      cmd = 0;
    }
  } else if (w_5abc0 & 0x8000) {
    cmd = 0x9C06;
  } else if (w_5abce & 0x20) {
    cmd = 0x4000;
  } else if (flags_word != 0 || b_5ab66 != 0) {
    w_5abc4 = uint16_t(w_5abc4 | 0x80);
    uint8_t m = b_5ab66;
    if (m & 0x10) cmd = 0x8010;
    else if (m & 0x20) cmd = 0x8012;
    else if (lo & 0x40) cmd = 0xC004;
    else if (lo & 0x80) cmd = 0xC001;
    else if (lo & 0x02) cmd = 0x8002;
    else if (m & 0x08) { w_5abc4 = uint16_t(w_5abc4 & ~0x80); cmd = 0x8024; }
    else if (m & 0x04) { w_5abc4 = uint16_t(w_5abc4 & ~0x80); cmd = 0x800E; }
    else if (lo & 0x01) cmd = 0x0C09;
    else if (m & 0x02) { w_5abc4 = uint16_t(w_5abc4 & ~0x80); cmd = 0x400D; }
  } else {
    if (!(w_5abc4 & 4)) cmd = 0;
    w_5abc4 = uint16_t(w_5abc4 & ~0x80);
  }

  if ((w_5adda & 0xFF) != (cmd & 0xFF)) {
    w_5add8 = cmd;
    w_5adda = cmd;
    w_5abc6 = uint16_t(w_5abc6 & ~0x2000);
    w_5abc4 = uint16_t(w_5abc4 & ~0x04);
    int16_t idx = int16_t(w_5add8 & 0xFF);
    int16_t off = int16_t(idx * 28);      // 16x + 8x + 4x, word arithmetic
    uint32_t rec = 0x521D0 + uint32_t(int32_t(off));
    b_5ab5a = rd8(rec);
    b_5ab5b = rd8(rec + 27);
    b_5ab8b = uint8_t(b_5ab5a & 0x3F);
  }

  if (w_5add8 & 0x8000) {
    if (!(w_5abc6 & 0x2000)) {
      w_5abc6 = uint16_t(w_5abc6 | 0x2000);
      b_5ab8a = (w_5abce & 0x20) ? b_5ae6c : b_5ab8b;
    }
    uint8_t mask = (w_5add8 & 0x1000) ? 4 : 2;
    if (b_5ab86 & mask) {
      if ((w_5abc6 & 0x20) && b_5aaa6 == 0) {
        w_5abc6 = uint16_t(w_5abc6 & ~0x20);
        if ((w_5add8 & 0xC000) == 0xC000) w_5add6 = 0;
        else w_5add6 = uint16_t(w_5add8 + 1);
        if (b_5ab5b != 0) wr8(0x5ae4f, rd8(0x5ae4f) | 0x08);  // bset #3
      } else {
        w_5abc6 = uint16_t(w_5abc6 | 0x20);
        w_5add6 = w_5add8;
        if (b_5ab5b != 0) wr8(0x5ae4f, rd8(0x5ae4f) | 0x04);  // bset #2
        if (b_5ab8b != 0) {
          int8_t n = int8_t(b_5ab8a - 1);
          b_5ab8a = uint8_t(n);
          if (n < 0) {
            uint16_t c = uint16_t(w_5add6 & 0xFF);
            w_5add6 = c;
            w_5adda = c;
            w_5add8 = c;
            w_5abc6 = uint16_t(w_5abc6 & ~0x2000);
            w_5abce = uint16_t(w_5abce & ~0x20);
            w_5abc4 = uint16_t(w_5abc4 & ~1);
          }
        }
      }
    }
  } else if (w_5add8 & 0x4000) {
    if (!(w_5abc6 & 0x2000)) {
      w_5abc6 = uint16_t(w_5abc6 | 0x2000);
      b_5ab8a = (w_5abce & 0x20) ? b_5ae6c : b_5ab8b;
      w_5add6 = w_5add8;
    }
    if (b_5ab86 & 0x02) {
      int8_t n = int8_t(b_5ab8a - 1);
      b_5ab8a = uint8_t(n);
      if (n < 0) {
        w_5add8 = 0;
        w_5add6 = 0;
        w_5abc6 = uint16_t(w_5abc6 & ~0x2000);
        w_5abce = uint16_t(w_5abce & ~0x20);
        w_5abc4 = uint16_t(w_5abc4 & ~1);
      }
    }
  } else {
    w_5add6 = w_5add8;
  }

  b_5ab5a = uint8_t(b_5ab5a & 0xC0);
  if (b_5ab58 != b_5ab5a) {
    b_5ab58 = b_5ab5a;
    b_5ab59 = 2;
  }
  b_5ab86 = 0;
}
NATIVE_C(0x23d54, decode_cockpit_command, false);

// $241f0: handle the "mode" key events in $5abbe: bit 1 toggles the
// map/HUD view ($5aa7d), bit 4 the flag $5aaa4.
static void handle_view_toggles() {
  if (b_5aa7d == 0 && b_5ab89 == 0) sub_2e662();
  uint8_t kind = uint8_t(rd8(scenario_rec(int16_t(w_5abd4)) + 98) & 0xF0);
  uint16_t ev = w_5abbe;
  if (ev & 0x02) {
    w_5abbe = uint16_t(ev & ~0x02);
    b_5aba8 = uint8_t(-2);
    if (b_5aa7d != 0) {
      b_5aa7d = 0;
      w_5ac7e = 0;
      b_5aa9e = 0;
      b_5ab2d = 1;
    } else {
      b_5aa7d = b_5ab2b;
      w_5ac7e = 50;
    }
    sub_29c60(3);
    if (kind == 48) w_5ac7e = 50;
    w_5ac80 = uint16_t(w_5ac7e << 4);
    sub_2f098();
    b_5aa9f = 0;
    b_5ab50 = uint8_t(-1);
    // $2e7e2 ends in the key-queue tail $2ef98, which uses d0 (the key code)
    // as the original C code left it: the low word is the value just
    // stored to $5ac80.
    set_d0_word(uint16_t(w_5ac80));
    sub_2e7e2();
    if (kind == 48 || b_5aa7d != 0) w_5ac7c = (b_5aaa5 != 0) ? 179 : 167;
  }
  if (kind == 48 && w_5ac7e == 0) {
    w_5ac7e = 50;
    w_5ac80 = 800;
    sub_2f098();
    b_5aa9f = 0;
    b_5ab50 = uint8_t(-1);
    set_d0_word(0);   // the original had just tested $5ac7e == 0 in d0
    sub_2e7e2();
    w_5ac7c = 167;
  }
  if (b_5aa7d != 0) {
    if (w_5abbe & 0x10) {
      w_5abbe = uint16_t(w_5abbe & ~0x10);
      b_5aaa4 = uint8_t(b_5aaa4 ^ 1);
    }
    if (b_5aba6 == 6 && int32_t(l_5af3a) < 147456 && w_5ac7c != 167) {
      w_5ac7c = 167;
      sub_2f02c();
    }
  }
}
NATIVE_C(0x241f0, handle_view_toggles, false);

// $2439a: leave the current scenario sub-view if its record does not allow
// it (bit 6 of the record's first word).
static void leave_disallowed_subview() {
  if (w_5abd4 == 0) return;
  if (rd16(scenario_rec(int16_t(w_5abd4))) & 0x40) return;
  w_5abd4 = 0;
  w_5abd6 = 0;
  b_5ab50 = uint8_t(-1);
  if (b_5aa7d == 0) {
    b_5aa9f = 0;
    w_5ac7e = 0;
    w_5ac80 = 0;
    sub_2e7e2();
  }
}
NATIVE_C(0x2439a, leave_disallowed_subview, false);

// $243fa: classify the player's heading (in 1/8 units of the 0..3600 circle)
// into the radar/alert flags $5ab63/$5ab64/$5aa7e and $5abc2 bit 1.
static void update_heading_alerts() {
  int16_t a, b;
  if (b_5aa7d != 0) {
    a = int16_t(int16_t(w_5ad8c) >> 3);
    b = 0;
  } else {
    a = int16_t(int32_t(l_5ad80) >> 3);
    b = int16_t(int32_t(l_5ad88) >> 3);
  }
  if (a > 600 && a < 3150) {
    if (b_5aa7e == 0) {
      b_5aa7e = 1;
      b_5ab50 = uint8_t(b_5ab50 | 11);
      b_5ab63 = 3;
    }
  } else {
    if (b_5aa7e != 0) {
      b_5aa7e = 0;
      b_5ab50 = uint8_t(b_5ab50 | 11);
    }
    if (a > 450 && a < 3200) {
      b_5ab64 = 1;
    } else {
      b_5ab64 = 0;
      if (a >= 3200) b_5ab63 = 0;
      else if (a > 300) b_5ab63 = 2;
      else if (a > 100) b_5ab63 = 1;
      else b_5ab63 = 0;
    }
  }
  w_5abc2 = uint16_t(w_5abc2 & ~2);
  bool flag = false;
  if (a >= 900 && a < 2700) {
    if (b < 900) flag = true;
    else if (b < 2700) flag = false;
    else flag = true;
  } else {
    if (b >= 900 && b <= 2700) flag = true;
  }
  if (flag) w_5abc2 = uint16_t(w_5abc2 | 2);
}
NATIVE_C(0x243fa, update_heading_alerts, false);

}  // namespace native
