// Amiga 500 machine model for running the original F/A-18 Interceptor code:
// 68000 core + custom chips (blitter, copper/display, Paula) + high-level
// emulation of the handful of AmigaOS library calls the game makes.
#pragma once

#include "m68k/cpu.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace amiga {

constexpr uint32_t RAM_SIZE = 8 * 1024 * 1024;

// NTSC timing
constexpr int LINES_PER_FRAME = 262;
constexpr int CYCLES_PER_LINE = 455;  // CPU clocks (227.5 colour clocks × 2)
constexpr double CPU_HZ = 7159090.0;
constexpr double PAULA_CLOCK = 3579545.0;

// Output framebuffer (lores pixels, display window origin at DIW $2C81)
constexpr int FB_W = 320;
constexpr int FB_H = 200;
constexpr int FB_HSTART = 0x81;  // lores pixel position of fb x=0
constexpr int FB_VSTART = 0x2C;  // beam line of fb y=0

struct QuitRequested {};

// INTENA/INTREQ bits
enum : uint16_t {
  INT_TBE = 1 << 0, INT_DSKBLK = 1 << 1, INT_SOFT = 1 << 2, INT_PORTS = 1 << 3,
  INT_COPER = 1 << 4, INT_VERTB = 1 << 5, INT_BLIT = 1 << 6,
  INT_AUD0 = 1 << 7, INT_AUD1 = 1 << 8, INT_AUD2 = 1 << 9, INT_AUD3 = 1 << 10,
  INT_RBF = 1 << 11, INT_DSKSYN = 1 << 12, INT_EXTER = 1 << 13, INT_INTEN = 1 << 14,
};

struct AudioChannel {
  uint32_t lc = 0;      // AUDxLC (reload)
  uint16_t len = 0;     // AUDxLEN (reload, words)
  uint16_t per = 0;
  uint16_t vol = 0;
  uint16_t dat = 0;
  // DMA state
  bool active = false;
  uint32_t pt = 0;
  uint32_t remaining = 0;  // words left in current block
  int byte_index = 0;      // 0/1 within the current word
  int8_t sample = 0;
  double phase = 0;        // fractional Paula ticks
};

class Machine;

// Host side of the machine (SDL front end).
struct Host {
  virtual ~Host() = default;
  // Called once per emulated frame with the 320×200 RGB888 framebuffer.
  virtual void frame(const uint32_t* fb) = 0;
  // Audio samples (interleaved stereo, signed 16-bit) produced this frame.
  virtual void audio(const int16_t* samples, size_t frames) = 0;
  virtual int audio_rate() const { return 48000; }
};

class Machine : public m68k::Bus {
public:
  Machine();
  ~Machine() override;

  // ---- setup ------------------------------------------------------------
  // Load the executable from `data_dir`/F-18 Interceptor and prepare the
  // initial process so that run() starts the game.
  bool boot(const std::string& data_dir);
  void set_host(Host* h) { host_ = h; }
  // Run until the game exits or QuitRequested is thrown by the host.
  int run();

  // ---- memory helpers for HLE code -------------------------------------
  uint8_t r8(uint32_t a) { return read8(a); }
  uint16_t r16(uint32_t a) { return read16(a); }
  uint32_t r32(uint32_t a) { return read32(a); }
  void w8(uint32_t a, uint8_t v) { write8(a, v); }
  void w16(uint32_t a, uint16_t v) { write16(a, v); }
  void w32(uint32_t a, uint32_t v) { write32(a, v); }
  std::string cstr(uint32_t a, size_t max = 256);
  std::string bstr(uint32_t bptr);

  // Simple chip-RAM allocator (first-fit, never compacts).
  uint32_t alloc(uint32_t size, bool clear = true);
  void free(uint32_t addr, uint32_t size);
  uint32_t avail() const;

  // Call 68k code at `addr`; registers d[]/a[] preloaded from the given
  // arrays (a7 is managed).  Returns d0. Z flag result in last_call_z_.
  uint32_t call68k(uint32_t addr, const uint32_t* d = nullptr, const uint32_t* a = nullptr);
  bool last_call_z() const { return last_call_z_; }

  // Advance emulated time without executing CPU code (used by blocking HLE
  // calls). Runs at least one scanline. Throws QuitRequested.
  void idle_until(const std::function<bool()>& done);
  void idle_lines(int n);

  // ---- input (called by host) -------------------------------------------
  void key_event(uint8_t rawkey, bool down);
  void mouse_move(int dx, int dy);
  void mouse_button(int button, bool down);  // 0 = left, 1 = right
  void joy_state(bool up, bool down, bool left, bool right, bool fire);

  // ---- bus ----------------------------------------------------------------
  uint8_t io_read8(uint32_t addr) override;
  uint16_t io_read16(uint32_t addr) override;
  void io_write8(uint32_t addr, uint8_t v) override;
  void io_write16(uint32_t addr, uint16_t v) override;

  m68k::Cpu cpu;
  bool trace = false;
  bool verbose = false;
  std::string data_dir;
  std::string save_dir;

  // Cycles per emulated frame given to the CPU (speed multiplier).
  double cpu_speed = 1.0;

  // Apply the codewheel protection bypass after loading.
  bool bypass_protection = true;
  // Map a WHDLoad PatchSeg offset (concatenated hunk payloads) to an address.
  uint32_t patch_address(uint32_t offset) const;

  // Exposed for debugging tools.
  std::vector<uint32_t> hunk_addrs;
  std::vector<uint32_t> hunk_sizes;

  void log(const char* fmt, ...);

private:
  friend struct Hle;
  // scheduler
  void run_line();
  void end_of_frame();
  void check_interrupts();
  void dispatch_interrupt(int level);
  void raise(uint16_t bits) { intreq_ |= bits; }

  // custom chips
  uint16_t custom_read(uint32_t reg);
  void custom_write(uint32_t reg, uint16_t v);
  void blit();
  void blit_line();
  void render_frame();
  void copper_reset();
  void copper_run_until(int vpos, int hpos);
  void render_line(int line);
  void audio_dma_start(int ch);
  void audio_advance(double paula_ticks, int16_t* out, int frames);
  void audio_line();

  bool line_a(uint16_t op);
  void hle(int lib, int lvo);

  std::vector<uint8_t> ram_;
  Host* host_ = nullptr;
  std::unique_ptr<struct Hle> hle_;

  // time
  int64_t line_start_ = 0;   // cpu.cycles at start of current line
  int vpos_ = 0;
  uint64_t frame_count_ = 0;
  bool exit_ = false;
  int exit_code_ = 0;
  int call_depth_ = 0;
  bool last_call_z_ = false;

  // custom registers
  uint16_t regs_[256] = {};
  uint16_t dmacon_ = 0, intena_ = 0, intreq_ = 0;
  uint16_t adkcon_ = 0;
  uint32_t cop1lc_ = 0, cop2lc_ = 0;
  uint32_t bplpt_[6] = {};
  uint16_t color_[32] = {};
  uint16_t bplcon0_ = 0, bplcon1_ = 0, bplcon2_ = 0;
  int16_t bpl1mod_ = 0, bpl2mod_ = 0;
  uint16_t diwstrt_ = 0x2C81, diwstop_ = 0xF4C1, ddfstrt_ = 0x38, ddfstop_ = 0xD0;
  // copper
  uint32_t coppc_ = 0;
  bool cop_waiting_ = false;
  bool cop_done_ = false;
  uint16_t cop_wait_v_ = 0, cop_wait_h_ = 0, cop_wait_mv_ = 0, cop_wait_mh_ = 0;
  // blitter
  uint16_t bltcon0_ = 0, bltcon1_ = 0, bltafwm_ = 0xFFFF, bltalwm_ = 0xFFFF;
  uint32_t bltpt_[4] = {};  // A B C D
  int16_t bltmod_[4] = {};
  uint16_t bltdat_[3] = {};  // A B C
  bool blt_zero_ = true;
  // display
  std::vector<uint32_t> fb_;
  // audio
  AudioChannel aud_[4];
  std::vector<int16_t> audio_buf_;
  double audio_frac_ = 0;
  // input
  int mouse_x_ = 0, mouse_y_ = 0;
  bool lmb_ = false, rmb_ = false;
  bool joy_up_ = false, joy_down_ = false, joy_left_ = false, joy_right_ = false, joy_fire_ = false;
  uint16_t potgo_ = 0;

  // heap
  std::map<uint32_t, uint32_t> free_list_;  // addr → size
};

}  // namespace amiga
