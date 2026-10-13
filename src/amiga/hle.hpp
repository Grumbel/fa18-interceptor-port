// High-level emulation of the AmigaOS libraries and devices used by the game.
#pragma once

#include "amiga/machine.hpp"

#include <cstdio>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace amiga {

// Line-A opcode $A<lib><idx>: lib 15 is reserved for host traps.
constexpr int HOST_LIB = 15;
enum { TRAP_RETURN = 0, TRAP_EXIT = 1 };

struct ReturnFromCall {};

enum Lib {
  LIB_EXEC = 0, LIB_DOS, LIB_GFX, LIB_INTUITION,
  LIB_INPUT, LIB_KEYBOARD, LIB_TIMER, LIB_GAMEPORT, LIB_AUDIO,
  LIB_RESOURCE, LIB_COUNT
};

struct Hle {
  explicit Hle(Machine& m) : m(m) {}

  void boot(uint32_t entry, uint32_t seglist_bptr);
  void call(int lib, int lvo);

  void line_tick(int vpos);
  void frame_tick();
  void interrupt(int level);
  bool interrupts_disabled() const;

  void key_event(uint8_t rawkey, bool down);
  void mouse_button(int button, bool down);
  void joy_fire(bool down);

  uint32_t ret_stub = 0;
  uint32_t exit_stub = 0;

private:
  Machine& m;
  uint32_t& D(int i) { return m.cpu.d[i]; }
  uint32_t& A(int i) { return m.cpu.a[i]; }

  uint32_t low_alloc(uint32_t size);
  uint32_t make_library(int lib, const char* name, int nvec, uint32_t possize);
  void unknown(int lib, int lvo);

  void exec(int lvo);
  void dos(int lvo);
  void gfx(int lvo);
  void intuition(int lvo);
  void device(int lib, int lvo);
  void resource(int lvo);

  // exec helpers
  void add_tail(uint32_t list, uint32_t node);
  void add_head(uint32_t list, uint32_t node);
  uint32_t rem_head(uint32_t list);
  void remove(uint32_t node);
  void enqueue(uint32_t list, uint32_t node);
  void new_list(uint32_t list);
  void put_msg(uint32_t port, uint32_t msg);
  void reply_msg(uint32_t msg);
  void signal(uint32_t task, uint32_t sigs);
  uint32_t wait(uint32_t sigs);
  void begin_io(uint32_t ioreq);
  void complete_io(uint32_t ioreq, int8_t error = 0);
  void abort_io(uint32_t ioreq);
  int lib_for_device(uint32_t dev) const;

  // devices
  void kbd_pump();
  void gameport_pump();
  void timer_pump();
  void write_input_event(uint32_t buf, uint8_t cls, uint16_t code, uint16_t qual,
                         int16_t x, int16_t y);
  uint64_t micros() const;

  // graphics
  void build_copper(uint32_t view);
  uint32_t copper_buffer(uint32_t view);

  // dos
  struct File {
    FILE* f = nullptr;
    std::string path;
  };
  std::string host_path(const std::string& amiga_name, bool for_write);
  uint32_t dos_open(const std::string& name, int mode);

  // memory layout
  uint32_t low_next_ = 0x400;
  uint32_t libs_[LIB_COUNT] = {};
  uint32_t task_ = 0;
  uint32_t input_task_ = 0;
  uint32_t cli_ = 0;

  // exec state
  uint32_t sig_alloc_ = 0xFFFF;   // low 16 reserved for the system
  uint32_t sig_recvd_ = 0;
  int disable_ = 0;
  int forbid_ = 0;
  int cur_level_ = 0;
  std::vector<uint32_t> servers_[16];  // AddIntServer chains (Interrupt*)
  uint32_t vectors_[16] = {};          // SetIntVector handlers

  // devices
  std::deque<uint32_t> kbd_reads_, gameport_reads_;
  std::deque<std::pair<uint8_t, bool>> kbd_queue_;
  std::deque<bool> fire_queue_;
  struct TimerReq { uint32_t io; uint64_t due; };
  std::vector<TimerReq> timers_;
  uint8_t key_matrix_[16] = {};
  uint16_t qualifier_ = 0;
  uint8_t gameport_type_ = 0;
  std::vector<uint32_t> input_handlers_;

  // graphics
  uint32_t active_view_ = 0;
  std::map<uint32_t, uint32_t> coplists_;  // view → copper buffer

  // dos
  std::map<uint32_t, File> files_;
  std::map<uint32_t, std::string> locks_;
  uint32_t next_handle_ = 1;
  uint32_t ioerr_ = 0;
};

}  // namespace amiga
