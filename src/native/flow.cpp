// Game flow: the per-frame driver and the state machine behind the mission
// selection screen, scenario start/end and the flight log.
// Original: hunk 0 $2112c-$23d08 (compiled C), translated from the assembly.
//
// The current state is a code address in game_state ($2a6d4); each frame
// run_game_state() decrements state_timer and calls the state handler.
// Handlers are registered at their original addresses so native and
// original handlers can be mixed (the state value stays an address).
#include "native/globals.hpp"
#include "native/orig.hpp"
#include "native/os.hpp"
#include "native/vars.hpp"

namespace native {
using namespace g;
using namespace v;

// ---- state machine ------------------------------------------------------------

inline constexpr Var<uint32_t> game_state{0x2a6d4};    // handler address
inline constexpr Var<int16_t> state_timer{0x5adce};    // frames, counts down
inline constexpr Var<int16_t> frame_counter{0x5abd2};
inline constexpr Var<int16_t> debug_stage{0x5adcc};    // progress marker in game_frame
inline constexpr Var<int8_t> in_flight{0x5aa8d};       // 3D world is running
inline constexpr Var<int16_t> message_queue{0x5aa42};  // [] message ids, 0-terminated
inline constexpr Var<int32_t> message_y{0x5aa36};
inline constexpr Var<int8_t> scenario{0x5ab9e};        // selected flight/mission

NATIVE_STATE(0x2196a, st_boot_check);
NATIVE_STATE(0x21a78, st_restart);
NATIVE_STATE(0x21a9e, st_21a9e);
NATIVE_STATE(0x21acc, st_21acc);
NATIVE_STATE(0x21aea, st_21aea);
NATIVE_STATE(0x21b5c, st_21b5c);
NATIVE_STATE(0x21ba4, st_21ba4);
NATIVE_STATE(0x21bd8, st_21bd8);
NATIVE_STATE(0x21cc8, st_21cc8);
NATIVE_STATE(0x21d0e, st_21d0e);
NATIVE_STATE(0x21d38, st_mission_menu);
NATIVE_STATE(0x21e0c, st_mission_menu_wait);
NATIVE_STATE(0x21f8e, st_flight_log);
NATIVE_STATE(0x22026, st_22026);
NATIVE_STATE(0x222d6, st_222d6);
NATIVE_STATE(0x22354, st_22354);
NATIVE_STATE(0x22380, st_22380);
NATIVE_STATE(0x223ca, st_223ca);
NATIVE_STATE(0x223f6, st_223f6);
NATIVE_STATE(0x2253c, st_2253c);
NATIVE_STATE(0x22570, st_22570);
NATIVE_STATE(0x225b0, st_225b0);
NATIVE_STATE(0x2261a, st_2261a);
NATIVE_STATE(0x226fe, st_226fe);
NATIVE_STATE(0x2274c, st_2274c);
NATIVE_STATE(0x2277e, st_2277e);
NATIVE_STATE(0x227a4, st_227a4);
NATIVE_STATE(0x227d0, st_227d0);
constexpr uint32_t st_22d60_addr = 0x22d60;
constexpr uint32_t st_228e2_addr = 0x228e2;
constexpr uint32_t st_2359e_addr = 0x2359e, st_2353c_addr = 0x2353c, st_2362a_addr = 0x2362a;

// $2346a: clear the message queue / text state.
static void clear_messages() {
  message_queue = 0;
  message_queue.at(1) = 0;
  message_y = 0x1b8;
  b_5aabe = 0;
  b_5aabb = 0;
  b_5aad8 = 0;
  b_5ab69 = 0;
}
NATIVE_C(0x2346a, clear_messages, true);

// $2151c: read input devices and feed key presses to the key handler.
static void poll_input() {
  sub_29006();                       // gameport (fire button) events
  int16_t mb = int16_t(sub_292b4()); // mouse buttons
  if (w_5aa76 != mb) {
    w_5aa76 = mb;
    w_5aa78 = mb;
    sub_25e8c();
  }
  int8_t mode = b_5ab43;
  if (mode == 1 || mode == 2) {
    while (w_5ac8e != 0) sub_2d984();
  } else if (mode == 3) {
    while ((w_5ac8e | w_5ac90) != 0) sub_2d984();
  }
  for (;;) {
    int8_t key = int8_t(sub_28dae());
    if (key == -1) break;
    sub_2dad0(uint32_t(uint8_t(key)));
  }
  if (mode == 0) {
    w_5ac8e = w_5ac92;
    w_5ac90 = w_5ac94;
    w_5ac92 = 0;
    w_5ac94 = 0;
  } else if (mode == 1) {
    w_5ac90 = w_5ac94;
    w_5ac94 = 0;
  }
}
NATIVE_C(0x2151c, poll_input, false);

// $215fe: stop all four sound channels.
static void stop_all_sounds() {
  for (int ch = 0; ch < 4; ++ch) sub_29c60(ch);
}
NATIVE_C(0x215fe, stop_all_sounds, false);

// $21630: open input devices, initialise controls and enter the boot state.
static void init_game() {
  sub_28e30();   // timer.device
  sub_28ce4();   // keyboard.device
  uint32_t input_task = os::FindTask(str_input_device);
  os::SetTaskPri(input_task, -128);
  sub_28ea4();   // gameport.device
  sub_397e8();
  sub_299d2();
  sub_2925c(uint32_t(-960), uint32_t(-960), 960, 960);   // mouse limits
  sub_29284(160, 100);                                     // mouse centre
  b_5ab43 = 0;
  sub_2d1a4();
  sub_2d178();
  game_state = st_boot_check_addr;
}
NATIVE_C(0x21630, init_game, false);

// $216c2: write `value` as `digits` hex digits ending at buf[digits-1];
// leading zeros (but the last digit) become spaces.
static void format_hex(uint32_t buf, uint32_t value, int8_t digits) {
  uint32_t p = buf + uint32_t(int32_t(digits));
  for (int8_t i = 0; i < digits; ++i) {
    uint8_t c = uint8_t((value & 15) + '0');
    if (c > '9') c = uint8_t(c + 7);
    wr8(p, c);
    --p;
    value >>= 4;
  }
  --digits;
  ++p;
  for (int8_t i = 0; i < digits && rd8(p) == '0'; ++i, ++p) wr8(p, ' ');
}
NATIVE_C(0x216c2, format_hex, true);

// $21750: per-frame state machine step (and flight time accounting).
static void run_game_state() {
  if (b_5ab90 >= 0 && scenario > 0) {
    if (b_5ab22 != 0 && b_5aa88 == 0 && b_5ab24 < 0 && l_5ac0c != 0) {
      // Accumulate flight time into the log (in seconds?).
      int32_t t = l_5adea - l_5ac0c - l_5ac00;
      if (l_5abfc != 0) t -= l_5adea - l_5abfc;
      t -= l_5ac08;
      if (l_5ac04 != 0) t -= l_5adea - l_5ac04;
      if (t < 0 || t >= 18000) {
        w_5ac96 = 63;
        sub_2f216();
      } else {
        wr32(config_buf + 8, rd32(config_buf + 8) + uint32_t(t));
        log_dirty = 1;
      }
      l_5ac0c = 0;
    }
    if (b_5aa90 >= 0) {
      if (b_5ab22 == -1) {
        b_5ab22 = 0;
        b_5ab23 = 0;
        game_state = st_restart_addr;
      } else if (b_5ab22 == 1) {
        b_5ab22 = 0;
        if (b_5aba4 != 0) b_5ab23 = 1;
        b_5aaa6 = 1;
        state_timer = 3;
        in_flight = 0;
        b_5ab99 = 0;
        game_state = st_21a9e_addr;
      } else if (b_5ab22 == 2) {
        b_5ab23 = 0;
        if (b_5ab24 < 0) {
          b_5aa90 = -16;
          b_5ab22 = 0;
          b_5aba5 = 1;
          b_5aaa6 = 1;
          state_timer = 6;
          game_state = 0x231a4;
        }
      }
    } else if (b_5ab22 == 3) {
      b_5ab23 = 0;
      if (b_5ab24 < 0) {
        b_5ab22 = 0;
        state_timer = w_5abb8;
        game_state = 0x231d0;
      }
    }
  }
  if (b_5ab24 >= 0) b_5ab24 = int8_t(b_5ab24 - 1);
  ++b_5aab9;
  state_timer = int16_t(state_timer - 1);
  call_c(game_state);
  b_5aa9b = 0;
}
NATIVE_C(0x21750, run_game_state, false);

// $2112c: one game frame: input, state machine, simulation and drawing.
static void game_frame() {
  int16_t frame = frame_counter;
  poll_input();
  run_game_state();
  sub_23c9c();
  if (in_flight) {
    debug_stage = 8;
    sub_241f0();
    debug_stage = 16;
    sub_3a312();
    sub_2f596();
    debug_stage = 32;
    sub_3a310();
    sub_4217c();
    sub_2f4a6();
    sub_39a54();
    sub_243fa();
    sub_2f7b8();
    debug_stage = 96;
    sub_39b06();
    sub_39dd0();
    sub_42f48();
    debug_stage = 104;
    if (b_5ab93 == 0 || b_5aaa8 != 0) sub_3f284();
    debug_stage = 112;
    sub_43306();
    if (l_5ad5e > -0x8000000) {
      debug_stage = 120;
      sub_2fa6c();
      debug_stage = 128;
      sub_2fa7e();
      debug_stage = 144;
      sub_3c1bc();
      debug_stage = 160;
      if (b_5aaa5 != 0) {
        sub_272e4();
      } else if (sub_3add8() != 0) {
        debug_stage = 164;
        sub_272e4();
        debug_stage = 168;
        sub_2fc14();
      } else {
        debug_stage = 172;
        sub_2fc14();
        debug_stage = 176;
        sub_272e4();
      }
    }
    debug_stage = 192;
    sub_23d54();
    sub_24aa8();
    debug_stage = 208;
    if (b_5aa7d == 0 || b_5ab2e != 0) {
      uint8_t c = rd8(0x5b47c + (uint32_t(int32_t(w_5abd4)) << 9) + 98);
      if (c != '0' || b_5ab2e != 0) {
        sub_45368();
        sub_455ba();
        sub_45e2a();
        sub_47ec0();
        sub_45d2e();
        sub_45b7c();
        int8_t boost = b_5ab2f;
        if (boost > 0 || (frame & 3) > 1) {
          sub_46aba();
          sub_46b50();
        }
        if (b_5ab2f > 0 || (frame & 3) < 2) sub_46d7c();
        if (b_5ab2f > 0 || (frame & 3) < 2 || l_5ad5e > -32768) sub_46c1e();
        if (b_5ab2f > 0 || (frame & 7) < 2) sub_46d2e();
        if (b_5ab2f > 0 || (frame & 15) < 2) sub_4551c();
        debug_stage = 416;
        sub_44c42();
        sub_474ac();
        sub_45604();
        sub_46dd6();
        sub_46e64();
        sub_466d0();
        sub_45938();
        sub_46668();
        sub_46ef2();
        sub_45760();
        if (b_5ab2f >= 0) b_5ab2f = int8_t(b_5ab2f - 1);
        debug_stage = 464;
      }
    } else if (b_5aa7d != 0) {
      b_5aad1 = 1;
      sub_46b50();
      sub_46c1e();
      sub_46aba();
      if (scenario == 2) sub_46ef2();
    }
    debug_stage = 468;
    sub_2439a();
    sub_46b4e();
    sub_3fd48();
    sub_3987e();
    if ((frame & 7) == 7) sub_399f6();
    if ((frame & 15) == 4) os::FreeSprite(0);
    if ((frame & 31) == 8) sub_4418a();
    else if (b_5aba6 == 0 && (frame & 31) == 16) sub_2f02c();
    if (b_5aaa6 == 0) frame_counter = int16_t(frame_counter + 1);
  } else {
    sub_24aa8();
    os::FreeSprite(0);
  }
  sub_3fba6();
  if (b_5aaaa != 0 && in_flight) {
    debug_stage = 528;
    sub_440a4();
    debug_stage = 536;
    sub_4677a();
  }
  debug_stage = 544;
  sub_478f2();
}
// Not registered yet: with this enabled, tests/compare_native.sh shows a one-byte
// divergence ($5ab55) around frame 2250 of free1 that is not yet explained
// (not a register dependency: FA18_POISON test is clean).
// NATIVE_C(0x2112c, game_frame, false);
[[maybe_unused]] static auto* game_frame_unused = &game_frame;

// ---- states ---------------------------------------------------------------------

// $2196a: first state: check the picture checksums (shows an error code if
// the files were modified), then go to the title/menu.
static void st_boot_check() {
  sub_2d1e6();
  uint32_t dst = fade_palette;
  for (int i = 0; i < 32; ++i) wr16(dst + uint32_t(i) * 2, rd16(0x3a054 + uint32_t(i) * 2));
  scenario = 0;
  b_5aaa5 = -1;
  b_5ab4f = -1;
  state_timer = 3;
  game_state = st_2359e_addr;
  uint32_t msg = 0x5433c + 21;  // "...X" error digit position
  uint32_t bad = 0;
  if (uint16_t(splash_checksum) != 0xC560) { wr8(msg, '1'); bad = uint16_t(splash_checksum); }
  if (uint16_t(cockpit_checksum) != 0x7E70) { wr8(msg, rd8(msg) | '2'); bad = uint16_t(cockpit_checksum); }
  if (uint16_t(front_checksum) != 0x4DE8) { wr8(msg, rd8(msg) | '4'); bad = uint16_t(front_checksum); }
  if (bad != 0) {
    format_hex(msg + 1, bad, 4);
    game_state = st_2353c_addr;
  } else {
    sub_29cee(50);
  }
}

static void st_restart() {
  sub_2d1e6();
  b_5aba4 = 0;
  scenario = 0;
  b_5ab43 = 0;
  game_state = st_mission_menu_addr;
}

static void st_21a9e() {
  if (state_timer < 0 && b_5ab98 == b_5ab99) {
    state_timer = 2;
    game_state = st_21acc_addr;
  }
}

static void st_21acc() {
  if (state_timer < 0) {
    in_flight = 1;
    game_state = st_21aea_addr;
  }
}

static void st_21aea() {
  stop_all_sounds();
  sub_2d1e6();
  if (scenario == 2) scenario = 125;
  if (b_5ab43 == 3) {
    b_5aaa6 = 1;
    state_timer = 2;
    b_5ab99 = 0;
    scenario = 127;
    game_state = st_21b5c_addr;
  } else {
    b_5ab43 = 0;
    sub_23c24(0x39fd4);
    game_state = st_mission_menu_wait_addr;
  }
}

// $21bfc: set up the demo flight.
static void start_demo() {
  b_5aba0 = b_5ab9f;
  b_5ab9f = 0;
  b_5ab40 = 4;
  b_5ab40 = 3;
  b_5aba6 = 0;
  b_5aaa5 = 0;
  b_5aa88 = 1;
  sub_3cf0e();
  scenario = 3;
  sub_2d50a();
  clear_messages();
  b_5aba5 = 0;
  in_flight = 1;
  w_5ac7e = 0;
  w_5ac80 = 0;
  sub_2f024();
  b_5ab50 = -1;
  state_timer = 1;
}
NATIVE_C(0x21bfc, start_demo, false);

static void st_21b5c() {
  if (state_timer < 0) {
    start_demo();
    b_5ab43 = 3;
    b_5aa7d = 0;
    state_timer = 2;
    b_5ab99 = 15;
    b_5ab98 = 0;
    game_state = st_21ba4_addr;
  } else {
    sub_4492a();
  }
}

static void st_21ba4() {
  if (state_timer < 0) {
    in_flight = 0;
    if (b_5ab98 == b_5ab99) {
      state_timer = 2;
      game_state = st_21bd8_addr;
    }
  }
}

static void st_21bd8() {
  if (state_timer < 0) {
    b_5aaa6 = 0;
    in_flight = 1;
    game_state = st_22d60_addr;
  }
}

static void st_21cc8() {
  if (state_timer < 0) {
    if (b_5ab23 == 0) {
      in_flight = 0;
      message_queue = 73;
      game_state = st_21d0e_addr;
    } else {
      in_flight = 1;
      clear_messages();
      game_state = st_22d60_addr;
    }
  }
}

static void st_21d0e() {
  if (b_5aad8 < 0 || b_5aa9b != 0) {
    in_flight = 1;
    clear_messages();
    game_state = st_22d60_addr;
  }
}

// $21d38: show the mission selection menu.
static void st_mission_menu() {
  music_volume2 = 0x1f0000;
  if ((sound_flags & 0x80) && title_flag_5ab47 == 0) music_volume = 0x1f0000;
  sub_29cee(15);
  title_flag_5ab47 = 1;
  sub_4492a();
  clear_messages();
  sub_208e2(0xc000);  // delay
  b_5aba5 = 0;
  sub_23c24(0x39fd4);
  message_y = 480;
  static const int16_t menu[] = {6, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 0};
  for (int i = 0; i < 12; ++i) message_queue.at(i) = menu[i];
  game_state = st_mission_menu_wait_addr;
}

// $21e0c: wait for a selection in the mission menu.
static void st_mission_menu_wait() {
  int i = 0;
  int8_t sel = scenario;
  if (b_5ab22 != 0) {
    b_5ab22 = 0;
    if (w_5aa6a != 6) {
      clear_messages();
      game_state = st_mission_menu_addr;
    }
    return;
  }
  if (scenario > 0) {
    music_volume2 = 0;
    clear_messages();
    sub_4492a();
    if (sel == 127) message_queue.at(i++) = 101;
    else if (sel == 1) message_queue.at(i++) = 102;
    else if (sel == 2) message_queue.at(i++) = 103;
    else if (sel == 125) message_queue.at(i++) = 104;
    else if (sel == 9) message_queue.at(i++) = 105;
    else if (b_5aa8a != 0) {
      message_queue.at(i++) = 107;
      b_5aa8a = 0;
    }
    message_queue.at(i) = 0;
    state_timer = (b_5ab23 == 0 && (sound_flags & 0x80)) ? 210 : 150;
    game_state = st_22026_addr;
    return;
  }
  if (scenario < 0) {
    clear_messages();
    sub_4492a();
    if (scenario == -1) {
      game_state = st_222d6_addr;
    } else {
      sub_4492a();
      sub_393f6();
      message_queue.at(i++) = 87;
      message_queue.at(i) = 0;
      game_state = st_flight_log_addr;
    }
    scenario = 0;
    return;
  }
  b_5ab22 = 0;
}

// $21f8e: flight log screen (1: save to disk, SHIFT-2: reset).
static void st_flight_log() {
  if (b_5aa8a != 0) {
    if (b_5aa8a == 1) {
      if (log_dirty != 0 && disk_state == 0) {
        sub_28592();   // save_flight_log
        b_5aacd = -1;
        if (rd16(config_buf + 4) <= 1) game_state = st_2362a_addr;
      }
    } else if (b_5aa8a == 2) {
      sub_2855e();     // reset_flight_log
      clear_messages();
      sub_4492a();
      sub_393f6();
      message_queue = 87;
    }
    b_5aa8a = 0;
  } else if (b_5ab22 != 0) {
    game_state = st_mission_menu_addr;
  }
  scenario = 0;
}

// $22026: after a positive mission selection, set up the scenario and hand
// off to the in-flight state chain.  Runs once when state_timer expires.
static void st_22026() {
  int16_t sel = int16_t(int8_t(scenario));
  if (state_timer >= 0) return;

  b_5aacf = 0;  // title_flag_5ab47
  sub_29c60(0);
  sub_29c60(1);
  music_volume = 0x3f0000;

  if (sel >= 3 && sel <= 8) {
    sub_29d82(40);
  } else if (rd8(0x5ae53) & 0x10) {
    sub_29fa2(8);
  } else {
    sub_29e4e(8, 0x300);
  }

  w_5ac7c = 179;
  clear_messages();
  state_timer = 4;
  b_5ab8f = 3;
  b_5ab90 = 2;
  sub_3cf0e();
  b_5aaa6 = 0;
  in_flight = 1;
  b_5ab2e = 3;
  b_5ab99 = b_5ab98;
  sub_23c66();
  game_state = st_2253c_addr;
  b_5ab40 = 3;
  b_5ab41 = 17;

  // Switch on the selected scenario.
  if (sel == 127) {
    b_5aaa6 = 1;
    game_state = st_21b5c_addr;
  } else if (sel == 1) {
    sub_2d3e0();
    b_5ab8f = 5;
    game_state = st_22354_addr;
  } else if (sel == 2) {
    w_5ac7c = 167;
    b_5aaac = -1;
    sub_3dc7c();
    b_5aaa5 = 0;
    b_5aa7d = 1;
    b_5aba6 = 0;
    b_5aaad = 1;
    w_5abd4 = 4;
    w_5abd6 = 2048;
    b_5abaa = 3;
    w_5bc7e = int16_t(uint16_t(w_5bc7e) | 0x100);
    game_state = st_223ca_addr;
  } else if (sel == 3) {
    b_5ab40 = 0;
  } else if (sel == 125) {
    b_5ab8f = 1;
    w_5ac7c = 167;
    b_5aaac = -1;
    b_5aaae = 3;
    b_5ab40 = 2;
    sub_22ce8();
    sub_3dc86();
    sub_2d408();
    sub_3cf0e();
    b_5aaa5 = 0;
    game_state = st_223ca_addr;
  } else if (sel == 9) {
    b_5ab40 = 3;
    b_5aba6 = 0;
    b_5aaa5 = 0;
    sub_3cf0e();
    sub_2d50a();
    clear_messages();
    b_5aba5 = 1;
    in_flight = 1;
    w_5ac7e = 0;
    w_5ac80 = 0;
    sub_2f024();
    b_5ab50 = -1;
    b_5aa7d = 0;
    game_state = st_22354_addr;
  }

  if (b_5ab40 == 3) b_5ab41 = 17;
  sub_2f7b8();
}

// $222d6: build the selectable-missions list from the flight log and return
// to the mission-menu wait state.
static void st_222d6() {
  sub_4492a();
  uint32_t q = message_queue.ptr();
  wr16(q, 64);  // header message
  q += 2;
  uint32_t src = 0x53ffc;  // mission-name id table (word per mission)
  for (int i = 3; i < 9; ++i) {
    // config_buf + 0x12 is the per-mission unlock flag array
    if (rd8(config_buf + 0x12 + uint32_t(i)) != 0) {
      wr16(q, rd16(src));
      q += 2;
      src += 2;
    }
    // locked slots do not consume a name-table entry (original behaviour)
  }
  wr16(q, 0x806e);  // "return" entry
  q += 2;
  wr16(q, 0);
  game_state = st_mission_menu_wait_addr;
}

// $22354 / $22380: free-flight intro — wait for key release, then arm flight.
static void st_22354() {
  if (state_timer < 0) {
    in_flight = 0;
    b_5ab99 = 15;
    b_5ab98 = 0;
    game_state = st_22380_addr;
  }
}
static void st_22380() {
  if (b_5ab98 == b_5ab99) {
    in_flight = 1;
    b_5ab50 = -1;
    b_5aba6 = 4;
    b_5aba5 = 1;
    state_timer = 5;
    game_state = st_227d0_addr;
  }
}

// $223ca / $223f6: combat-mission intro (scenarios 2 / 125).
static void st_223ca() {
  if (state_timer < 0) {
    in_flight = 0;
    b_5ab99 = 15;
    b_5ab98 = 0;
    game_state = st_223f6_addr;
  }
}
static void st_223f6() {
  if (b_5ab98 == b_5ab99) {
    in_flight = 1;
    b_5ab50 = -1;
    b_5aba5 = 1;
    state_timer = 5;
    game_state = st_227d0_addr;
  }
}

// $2253c / $22570: default post-menu wait (most scenarios).
static void st_2253c() {
  b_5aba5 = 1;
  if (state_timer < 0) {
    in_flight = 0;
    b_5ab99 = 15;
    b_5ab98 = 0;
    game_state = st_22570_addr;
  }
}
static void st_22570() {
  if (b_5ab98 == b_5ab99) {
    in_flight = 1;
    b_5ab50 = -1;
    b_5aaa5 = 1;
    b_5ab4f = 1;
    state_timer = 9;
    game_state = st_225b0_addr;
  }
}

// $225b0: after key release, either abort on fire/escape or advance.
static void st_225b0() {
  b_5aa8e = int8_t(uint8_t(b_5aa8e) | uint8_t(b_5aa9b));
  if (b_5ab23 == 0 && b_5aa9b != 0) {
    in_flight = 1;
    clear_messages();
    state_timer = -1;
    game_state = st_2274c_addr;
    return;
  }
  if (state_timer < 0) {
    b_5ab4f = 0;
    state_timer = 1;
    game_state = st_2261a_addr;
  }
}

// $2261a: after the post-menu wait, either abort on fire or show the
// mission-unlock / briefing text for scenarios 3–8.
static void st_2261a() {
  b_5aaa6 = 0;
  b_5aa8e = int8_t(uint8_t(b_5aa8e) | uint8_t(b_5aa9b));
  if (b_5ab23 == 0 && b_5aa9b != 0) {
    in_flight = 1;
    clear_messages();
    state_timer = -1;
    game_state = st_2274c_addr;
    return;
  }
  if (state_timer < 0) {
    in_flight = 0;
    int16_t sel = int16_t(int8_t(scenario));
    if (sel >= 3 && sel <= 8) {
      message_queue = 91;
      uint8_t unlocked = rd8(config_buf + 0x12 + uint32_t(sel));
      if (unlocked == 0) {
        sub_39510(uint32_t(sel), 1, 0);
      } else {
        sub_39510(uint32_t(sel), 1, 1);
      }
    }
    game_state = st_226fe_addr;
  }
}

// $226fe: wait for message / input, then enter the pre-flight fade.
static void st_226fe() {
  if (b_5aad8 < 0 || b_5aa9b != 0) {
    b_5aa8e = int8_t(uint8_t(b_5aa8e) | uint8_t(b_5aa9b));
    in_flight = 1;
    clear_messages();
    state_timer = -1;
    b_5ab2e = 3;
    game_state = st_2274c_addr;
  }
}

// $2274c: short timer, then enable radar / view flags.
static void st_2274c() {
  if (state_timer < 0) {
    b_5aaa5 = 1;
    b_5ab4f = 1;
    state_timer = 1;
    sub_3db54();
    game_state = st_2277e_addr;
  }
}

// $2277e: set view mode 2 and fire-button latch.
static void st_2277e() {
  if (state_timer < 0) {
    b_5aba6 = 2;
    b_5aaad = 1;
    game_state = st_227a4_addr;
  }
}

// $227a4: wait until view mode reaches 3, then clear latch and continue.
static void st_227a4() {
  if (b_5aba6 == 3) {
    b_5aaad = 0;
    b_5ab4f = 0;
    state_timer = 2;
    game_state = st_227d0_addr;
  }
}

// $227d0: final pre-flight gate — either jump to codewheel path or show
// mission briefing messages and hand off to $228e2 (not yet translated).
static void st_227d0() {
  if (state_timer >= 0) return;
  in_flight = 0;
  if (b_5ab23 != 0) {
    b_5aba4 = 1;
    b_5aba5 = 2;
    state_timer = 3;
    game_state = st_228e2_addr;
    return;
  }
  // Build a short message queue from the scenario id (missions 3–8).
  uint32_t q = message_queue.ptr();
  int16_t sel = int16_t(int8_t(scenario));
  if (b_5aa8e == 0 && sel >= 3 && sel <= 8) {
    if (sel == 3) {
      wr16(q, 95);
      q += 2;
    } else {
      wr16(q, 96);
      q += 2;
    }
  }
  b_5aa8e = 0;
  wr16(q, 71);
  q += 2;
  wr16(q, 0);
  sub_396e2();
  game_state = 0x22886;  // next micro-state; leave as address until translated
}

}  // namespace native
