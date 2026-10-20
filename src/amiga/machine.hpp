// Amiga 500 machine model for running the original F/A-18 Interceptor code:
// 68000 core + custom chips (blitter, copper/display, Paula) + high-level
// emulation of the handful of AmigaOS library calls the game makes.
#pragma once

#include "m68k/cpu.hpp"
#include "recomp/recomp.hpp"
#include "amiga/adf.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace native { struct Def; }

namespace amiga { class Machine; }
namespace remaster {
void install(amiga::Machine& m);
// Build the composed hi-res frame; false if not available this frame.
bool compose(amiga::Machine& m, std::vector<uint32_t>& out);
// Forget all per-frame layers (after a state load).
void reset();
}

namespace amiga {

constexpr uint32_t RAM_SIZE = 8 * 1024 * 1024;

// NTSC timing
constexpr int LINES_PER_FRAME = 262;
constexpr int CYCLES_PER_LINE = 455;  // CPU clocks (227.5 colour clocks × 2)
constexpr double CPU_HZ = 7159090.0;
constexpr double PAULA_CLOCK = 3579545.0;

// Output framebuffer (lores pixels, display window origin at DIW $2A81)
constexpr int FB_W = 320;
constexpr int FB_H = 200;
constexpr int FB_HSTART = 0x81;  // lores pixel position of fb x=0
// All of the game's screens use the display window $2A81-$F2C1, two
// lines above the OS default.
constexpr int FB_VSTART = 0x2A;  // beam line of fb y=0
constexpr int VIEW_DYOFFSET = 0x2C;  // InitView default (NTSC)

struct QuitRequested {};
// Thrown to unwind the native C++ stack when a state is to be loaded while
// the game is not (yet) in its main loop (e.g. during the intro).
struct LoadRequested {};

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
  // Remaster: same frame at a higher resolution (w x h RGB888).
  virtual void frame_hires(const uint32_t* fb, int w, int h) { (void)fb; (void)w; (void)h; }
  // Audio samples (interleaved stereo, signed 16-bit) produced this frame.
  virtual void audio(const int16_t* samples, size_t frames) = 0;
  virtual int audio_rate() const { return 48000; }
};

class Machine : public m68k::Bus {
public:
  Machine();
  ~Machine() override;

  // ---- setup ------------------------------------------------------------
  // Load the executable and prepare the initial process so that run()
  // starts the game.  `path` may be either:
  //   - a directory containing the unpacked disk files (legacy), or
  //   - a path to the original .adf disk image (preferred; no unpack needed).
  bool boot(const std::string& path);
  void set_host(Host* h) { host_ = h; }

  // When boot() was given an ADF, these serve the original disk files.
  // Otherwise nullptr / empty — HLE falls back to the host filesystem.
  const Adf* adf() const { return adf_.get(); }
  bool disk_has(const std::string& amiga_path) const;
  std::vector<uint8_t> disk_read(const std::string& amiga_path) const;
  int32_t disk_size(const std::string& amiga_path) const;
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
  // Take `size` bytes from the top of RAM, away from the game's own
  // allocations (they keep their addresses); not part of save states or
  // RAM hashes.  For data installed once at boot (world overrides).
  uint32_t reserve_top(uint32_t size);
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

  // Remaster option (default off = original behaviour): analog flight stick.
  // x: -1 left .. +1 right, y: -1 up .. +1 down (SDL convention).  While
  // analog_active the native control code replaces the digitally ramped
  // elevator/rudder deflections of the player aircraft.
  // Save states: requests are applied at the next main-loop iteration.
  void request_save_state(const std::string& path) { pending_save_ = path; }
  void request_load_state(const std::string& path) { pending_load_ = path; }
  std::string take_state_message() { std::string s; s.swap(state_msg_); return s; }
  // Called by the game's main loop ($27d50) at the top of every iteration,
  // the only point where all state is in RAM/registers (no native C++
  // routine in the middle of its work).
  void apply_state_requests();
  bool in_main_loop = false;      // set by the game's main loop ($27d50)
  bool save_state_now(const std::string& path);
  bool load_state_now(const std::string& path);

  // Register an observer for the routine entry at `addr` (see execute()).
  void add_pc_hook(uint32_t addr, std::function<void()> fn);

  uint64_t frame_count() const { return frame_count_; }
  uint16_t color_reg(int i) const { return cs_.color_[i & 31]; }
  // Scan-out data of the last rendered frame (see remaster::compose)
  const uint32_t* frame_rgb() const { return fb_.data(); }
  const uint8_t* frame_idx() const { return idx_.data(); }
  const uint32_t* frame_row_key() const { return row_key_.data(); }
  const uint32_t* frame_row_pal() const { return row_pal_.data(); }
  int hires_scale = 0;  // remaster: 3D view render scale (0 = off)
  // Switch the hi-res 3D view on/off at run time (installs the observers).
  void set_hires_scale(int s);
  bool analog_enabled = false;
  // Remaster option (debug): draw every object at its nearest level of
  // detail and the terrain with its full meshes (render.cpp, ground.cpp).
  bool full_detail = false;
  // Remaster option: pace the text streams like a stock A500 (menu screens
  // at most ~180 passes/s, in-flight messages at most ~10 steps/s);
  // game_frame in flow.cpp.
  bool text_pace = false;
  bool analog_active = false;
  float analog_x = 0.0f, analog_y = 0.0f;

  // ---- bus ----------------------------------------------------------------
  uint8_t io_read8(uint32_t addr) override;
  uint16_t io_read16(uint32_t addr) override;
  void io_write8(uint32_t addr, uint8_t v) override;
  void io_write16(uint32_t addr, uint16_t v) override;

  m68k::Cpu cpu;
  bool trace = false;
  bool verbose = false;
  std::string data_dir;   // directory mode: path to unpacked files; ADF mode: path to .adf
  std::string save_dir;
  bool data_is_adf = false;  // true when data_dir points at an ADF image

  // CPU speed multiplier: the CPU gets cpu_speed × 455 cycles per scanline.
  void set_cpu_speed(double s) { line_cycles_ = int64_t(CYCLES_PER_LINE * (s < 0.1 ? 0.1 : s)); }
  // Emulated time since boot in microseconds (scanline based).
  uint64_t micros() const {
    uint64_t lines = vbl_time ? total_lines_ / LINES_PER_FRAME * LINES_PER_FRAME : total_lines_;
    return uint64_t(double(lines) * 1e6 / (LINES_PER_FRAME * 60.0));
  }
  // Deterministic timing for comparing code variants: CPU instructions take
  // no time, every OS call takes one scanline (time only advances in OS
  // calls, so equivalent code produces identical runs).
  void set_det_timing(bool on) { det_timing_ = on; if (on) line_cycles_ = int64_t(1) << 50; }
  bool det_timing() const { return det_timing_; }
  // Quantise OS time to whole frames (deterministic comparisons between
  // code variants that take different numbers of CPU cycles).
  bool vbl_time = false;

  // Apply the codewheel protection bypass after loading.
  bool bypass_protection = true;
  // Map a WHDLoad PatchSeg offset (concatenated hunk payloads) to an address.
  uint32_t patch_address(uint32_t offset) const;

  // Hand-translated C++ replacements of game routines (src/native).
  enum class NativeMode { Off, On, Verify };
  NativeMode native_mode = NativeMode::On;
  // Run the routine at addr with the current registers/stack until it
  // returns (RTS to a host trap). Used by native code to call original code.
  void call_sub(uint32_t addr);
  void print_native_stats();

  // Use statically recompiled code when available (else interpret).
  bool use_recomp = true;
  bool recomp_active() const { return !recomp_table_.empty(); }
  // Collect addresses inside the program that had to be interpreted.
  void track_recomp_misses(bool on) { recomp_misses_ = on ? std::make_unique<std::set<uint32_t>>() : nullptr; }
  const std::set<uint32_t>* recomp_misses() const { return recomp_misses_.get(); }

  // Coverage: record executed instruction addresses (debug/recompiler seeds)
  // and report bytes of code hunks that changed since load.
  std::string coverage_path;
  std::vector<uint8_t> executed;  // per byte of RAM below the heap
  std::vector<uint8_t> original;  // loaded image for SMC detection
  void write_coverage();

  uint64_t loadview_count = 0;  // game frames presented (stats)
  int ram_hash_every = 0;       // log a hash of game memory every N frames
  uint64_t ram_dump_frame = 0;  // dump RAM to ram_dump_path at this frame
  std::string ram_dump_path;

  // Exposed for debugging tools.
  std::vector<uint32_t> hunk_addrs;
  std::vector<uint32_t> hunk_sizes;

  void log(const char* fmt, ...);

private:
  friend struct Hle;
  // scheduler
  void execute(int64_t until);
  void run_line();
  void end_of_frame();
  void check_interrupts();
  void dispatch_interrupt(int level);
  void raise(uint16_t bits) { cs_.intreq_ |= bits; }

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
  void setup_recomp(uint32_t end);
  void setup_native();
  void invoke_native(const native::Def* d);
  void verify_native(const native::Def* d);
  struct Snapshot;
  void take_snapshot(Snapshot& s);
  void restore_snapshot(const Snapshot& s);
  void hle(int lib, int lvo);

  std::vector<uint8_t> ram_;
  Host* host_ = nullptr;
  std::unique_ptr<struct Hle> hle_;
  std::unique_ptr<Adf> adf_;

  // time
  int64_t line_start_ = 0;   // cpu.cycles at start of current line
  int64_t line_cycles_ = CYCLES_PER_LINE;
  bool det_timing_ = false;
  uint64_t total_lines_ = 0;
  int vpos_ = 0;
  uint64_t frame_count_ = 0;
  bool exit_ = false;
  int exit_code_ = 0;
  int call_depth_ = 0;
  bool last_call_z_ = false;

  // Custom chip state (one struct so it can be snapshotted).
  struct CustomState {
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
    uint16_t potgo_ = 0;
    AudioChannel aud_[4];
  } cs_;
  // display
  std::vector<uint32_t> fb_;
  std::vector<uint32_t> hires_fb_;
  std::string pending_save_, pending_load_, state_msg_;
  bool resume_main_loop_ = false;
  // Remaster scan-out records (filled only when hires_scale > 0)
  std::vector<uint8_t> idx_;          // palette index per low-res pixel
  std::vector<uint32_t> row_key_;     // plane-0 pointer at the start of each row
  std::vector<uint32_t> row_pal_;     // 32 RGB entries per row
  // audio
  std::vector<int16_t> audio_buf_;
  double audio_frac_ = 0;
  double lp_l_ = 0, lp_r_ = 0, lp_k_ = 0.4;
  // input
  int mouse_x_ = 0, mouse_y_ = 0;
  bool lmb_ = false, rmb_ = false;
  bool joy_up_ = false, joy_down_ = false, joy_left_ = false, joy_right_ = false, joy_fire_ = false;

  // heap
  std::map<uint32_t, uint32_t> free_list_;  // addr → size

  // native code
  std::vector<const native::Def*> native_table_;
  // Observation hooks (remaster): called when execution reaches an address,
  // before the routine runs; they must not change machine state.
  std::vector<std::function<void()>*> pc_hooks_;
  bool in_reference_ = false;
 public:
  uint32_t native_entry_sp_ = 0;  // a7 at entry of the running native C function
 private:
  bool calltrace_ = false;
  uint32_t watch_addr_ = 0, watch_val_ = 0;
  uint32_t regs_addr_ = 0;
  int regs_log_budget_ = 0;   // running original code for --native verify
  bool suppress_irq_ = false;
  uint32_t heap_hw_ = 0;        // highest heap address handed out
  struct NativeStats { uint64_t calls = 0, verified = 0, mismatches = 0, skipped = 0; };
  std::map<const native::Def*, NativeStats> native_stats_;

  // recompiled code
  std::vector<recomp::Fn> recomp_table_;
  std::unique_ptr<std::set<uint32_t>> recomp_misses_;
};

}  // namespace amiga
