// AmigaOS high-level emulation: exec, dos, graphics, intuition and the
// input/keyboard/timer/gameport/audio devices, as far as the game uses them.
#include "amiga/hle.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace amiga {

namespace {
// Exec structure offsets
constexpr uint32_t LN_SUCC = 0, LN_PRED = 4, LN_TYPE = 8, LN_PRI = 9, LN_NAME = 10;
constexpr uint32_t LH_HEAD = 0, LH_TAIL = 4, LH_TAILPRED = 8;
constexpr uint32_t MP_FLAGS = 14, MP_SIGBIT = 15, MP_SIGTASK = 16, MP_MSGLIST = 20;
constexpr uint32_t MN_REPLYPORT = 14;
constexpr uint32_t IO_DEVICE = 20, IO_UNIT = 24, IO_COMMAND = 28, IO_FLAGS = 30, IO_ERROR = 31;
constexpr uint32_t IO_ACTUAL = 32, IO_LENGTH = 36, IO_DATA = 40;
constexpr uint32_t IS_DATA = 14, IS_CODE = 18;
constexpr uint8_t NT_TASK = 1, NT_INTERRUPT = 2, NT_DEVICE = 3, NT_MSGPORT = 4, NT_MESSAGE = 5,
                  NT_REPLYMSG = 7, NT_LIBRARY = 9, NT_PROCESS = 13;
constexpr uint8_t IOF_QUICK = 1;
constexpr uint16_t CMD_READ = 2, CMD_WRITE = 3, CMD_UPDATE = 4, CMD_CLEAR = 5, CMD_STOP = 6,
                   CMD_START = 7, CMD_FLUSH = 8;
constexpr int8_t IOERR_OPENFAIL = -1, IOERR_ABORTED = -2, IOERR_NOCMD = -3;

constexpr uint32_t SSP_TOP = 0xF000;
constexpr uint32_t USP_TOP = 0x1FFF0;
constexpr uint32_t STACK_SIZE = 0x10000 - 0x10;

const char* lib_name(int lib) {
  static const char* names[] = {"exec", "dos", "graphics", "intuition", "input.device",
                                "keyboard.device", "timer.device", "gameport.device",
                                "audio.device", "resource"};
  return lib < LIB_COUNT ? names[lib] : "?";
}
}  // namespace

// ---- setup ------------------------------------------------------------------

uint32_t Hle::low_alloc(uint32_t size) {
  uint32_t a = (low_next_ + 3) & ~3u;
  low_next_ = a + size;
  if (low_next_ > SSP_TOP - 0x1000) m.log("low memory exhausted\n");
  std::memset(&m.ram[a], 0, size);
  return a;
}

uint32_t Hle::make_library(int lib, const char* name, int nvec, uint32_t possize) {
  uint32_t neg = uint32_t(nvec) * 6;
  uint32_t block = low_alloc(neg + possize + 64);
  uint32_t base = block + neg;
  for (int i = 1; i <= nvec; ++i) {
    uint32_t v = base - uint32_t(i) * 6;
    m.w16(v, uint16_t(0xA000 | (lib << 8) | i));
    m.w16(v + 2, 0x4E75);  // rts
    m.w16(v + 4, 0x4E71);
  }
  uint32_t name_addr = base + possize;
  for (size_t i = 0; name[i]; ++i) m.w8(name_addr + uint32_t(i), uint8_t(name[i]));
  m.w8(base + LN_TYPE, lib >= LIB_INPUT && lib <= LIB_AUDIO ? NT_DEVICE : NT_LIBRARY);
  m.w32(base + LN_NAME, name_addr);
  m.w16(base + 16, uint16_t(neg));
  m.w16(base + 18, uint16_t(possize));
  m.w16(base + 20, 34);  // version (Kickstart 1.3)
  m.w16(base + 22, 5);
  m.w32(base + 24, name_addr);
  libs_[lib] = base;
  return base;
}

void Hle::boot(uint32_t entry, uint32_t seglist_bptr) {
  // Host trap stubs
  ret_stub = low_alloc(4);
  m.w16(ret_stub, uint16_t(0xA000 | (HOST_LIB << 8) | TRAP_RETURN));
  exit_stub = low_alloc(4);
  m.w16(exit_stub, uint16_t(0xA000 | (HOST_LIB << 8) | TRAP_EXIT));

  uint32_t exec = make_library(LIB_EXEC, "exec.library", 130, 640);
  m.w32(4, exec);
  m.w8(exec + 530, 60);  // VBlankFrequency
  m.w8(exec + 531, 60);  // PowerSupplyFrequency
  m.w16(exec + 296, 0);  // AttnFlags: plain 68000
  make_library(LIB_DOS, "dos.library", 40, 64);
  uint32_t gfx = make_library(LIB_GFX, "graphics.library", 110, 0x200);
  m.w16(gfx + 206, 1);    // DisplayFlags: NTSC
  m.w16(gfx + 212, 261);  // MaxDisplayRow
  m.w16(gfx + 214, 0x1C7);
  m.w16(gfx + 216, 200);  // NormalDisplayRows
  m.w16(gfx + 218, 640);  // NormalDisplayColumns
  m.w16(gfx + 232, 63);   // MicrosPerLine
  m.w32(gfx + 42, 0xBFE001);  // cia
  m.w32(gfx + 46, 0xDFF000);  // blitter
  make_library(LIB_INTUITION, "intuition.library", 70, 0x60);
  make_library(LIB_INPUT, "input.device", 8, 64);
  make_library(LIB_KEYBOARD, "keyboard.device", 8, 64);
  make_library(LIB_TIMER, "timer.device", 12, 64);
  make_library(LIB_GAMEPORT, "gameport.device", 8, 64);
  make_library(LIB_AUDIO, "audio.device", 8, 64);
  make_library(LIB_RESOURCE, "generic.resource", 8, 64);

  // Process structure
  task_ = low_alloc(256);
  uint32_t tname = low_alloc(32);
  const char* nm = "F-18 Interceptor";
  for (size_t i = 0; nm[i]; ++i) m.w8(tname + uint32_t(i), uint8_t(nm[i]));
  m.w8(task_ + LN_TYPE, NT_PROCESS);
  m.w32(task_ + LN_NAME, tname);
  m.w8(task_ + 15, 2);  // TS_RUN
  m.w32(task_ + 18, sig_alloc_);
  m.w32(task_ + 58, USP_TOP - STACK_SIZE);
  m.w32(task_ + 62, USP_TOP);
  // pr_MsgPort
  uint32_t port = task_ + 92;
  m.w8(port + LN_TYPE, NT_MSGPORT);
  m.w8(port + MP_SIGBIT, 8);
  m.w32(port + MP_SIGTASK, task_);
  new_list(port + MP_MSGLIST);
  m.w32(task_ + 128, seglist_bptr);
  m.w32(task_ + 132, STACK_SIZE);
  m.w32(task_ + 152, 0);  // pr_CurrentDir
  // CLI structure so the startup code takes the CLI path
  cli_ = low_alloc(64);
  uint32_t cmdname = low_alloc(40);
  m.w8(cmdname, uint8_t(std::strlen(nm)));
  for (size_t i = 0; nm[i]; ++i) m.w8(cmdname + 1 + uint32_t(i), uint8_t(nm[i]));
  m.w32(cli_ + 16, cmdname >> 2);
  m.w32(cli_ + 60, seglist_bptr);
  m.w32(task_ + 172, cli_ >> 2);
  m.w32(exec + 276, task_);  // ThisTask

  input_task_ = low_alloc(96);
  uint32_t iname = low_alloc(16);
  const char* in = "input.device";
  for (size_t i = 0; in[i]; ++i) m.w8(iname + uint32_t(i), uint8_t(in[i]));
  m.w8(input_task_ + LN_TYPE, NT_TASK);
  m.w8(input_task_ + LN_PRI, 20);
  m.w32(input_task_ + LN_NAME, iname);

  // Command line arguments: "\n"
  uint32_t args = low_alloc(4);
  m.w8(args, '\n');

  // CPU state: user mode on the process stack.
  auto& c = m.cpu;
  c.s = false;
  c.osp = SSP_TOP;
  c.a[7] = USP_TOP;
  c.push32(STACK_SIZE);
  c.push32(exit_stub);
  c.d[0] = 1;
  c.a[0] = args;
  c.pc = entry;
  c.imask = 0;
}

// ---- dispatch ---------------------------------------------------------------

void Hle::unknown(int lib, int lvo) {
  m.log("HLE: unimplemented %s LVO -%d (called from %06x)\n", lib_name(lib), lvo,
        m.r32(m.cpu.a[7]));
  D(0) = 0;
}

void Hle::call(int lib, int lvo) {
  if (m.det_timing()) {
    // In deterministic timing mode only waiting/polling calls take time
    // (one scanline), so code that does I/O differently stays comparable.
    bool waits = false;
    if (lib == LIB_EXEC)
      waits = lvo == 318 || lvo == 372 || lvo == 384 || lvo == 456 || lvo == 468 || lvo == 474;
    else if (lib == LIB_GFX)
      waits = lvo == 402 || lvo == 270;
    else if (lib == LIB_DOS)
      waits = lvo == 198;
    if (waits) m.idle_lines(1);
  }
  if (m.verbose && !(lib == LIB_GFX && (lvo == 228 || lvo == 402)))
    m.log("[hle] %s -%d from %06x\n", lib_name(lib), lvo, m.r32(m.cpu.a[7]));
  switch (lib) {
    case LIB_EXEC: exec(lvo); break;
    case LIB_DOS: dos(lvo); break;
    case LIB_GFX: gfx(lvo); break;
    case LIB_INTUITION: intuition(lvo); break;
    case LIB_RESOURCE: resource(lvo); break;
    default: device(lib, lvo); break;
  }
}

// ---- exec lists / messages ------------------------------------------------

void Hle::new_list(uint32_t l) {
  m.w32(l + LH_HEAD, l + LH_TAIL);
  m.w32(l + LH_TAIL, 0);
  m.w32(l + LH_TAILPRED, l + LH_HEAD);
}

void Hle::add_tail(uint32_t l, uint32_t n) {
  uint32_t tp = m.r32(l + LH_TAILPRED);
  m.w32(n + LN_SUCC, l + LH_TAIL);
  m.w32(n + LN_PRED, tp);
  m.w32(tp + LN_SUCC, n);
  m.w32(l + LH_TAILPRED, n);
}

void Hle::add_head(uint32_t l, uint32_t n) {
  uint32_t h = m.r32(l + LH_HEAD);
  m.w32(n + LN_SUCC, h);
  m.w32(n + LN_PRED, l + LH_HEAD);
  m.w32(h + LN_PRED, n);
  m.w32(l + LH_HEAD, n);
}

uint32_t Hle::rem_head(uint32_t l) {
  uint32_t h = m.r32(l + LH_HEAD);
  uint32_t next = m.r32(h + LN_SUCC);
  if (!next) return 0;
  m.w32(l + LH_HEAD, next);
  m.w32(next + LN_PRED, l + LH_HEAD);
  return h;
}

void Hle::remove(uint32_t n) {
  uint32_t succ = m.r32(n + LN_SUCC), pred = m.r32(n + LN_PRED);
  if (!succ || !pred) return;
  m.w32(pred + LN_SUCC, succ);
  m.w32(succ + LN_PRED, pred);
}

void Hle::enqueue(uint32_t l, uint32_t n) {
  int8_t pri = int8_t(m.r8(n + LN_PRI));
  uint32_t cur = m.r32(l + LH_HEAD);
  while (m.r32(cur + LN_SUCC)) {
    if (int8_t(m.r8(cur + LN_PRI)) < pri) break;
    cur = m.r32(cur + LN_SUCC);
  }
  // insert before cur
  uint32_t pred = m.r32(cur + LN_PRED);
  m.w32(n + LN_SUCC, cur);
  m.w32(n + LN_PRED, pred);
  m.w32(pred + LN_SUCC, n);
  m.w32(cur + LN_PRED, n);
}

void Hle::signal(uint32_t task, uint32_t sigs) {
  if (task == task_ || task == 0) sig_recvd_ |= sigs;
}

uint32_t Hle::wait(uint32_t sigs) {
  if (!sigs) return 0;
  m.idle_until([&] { return (sig_recvd_ & sigs) != 0; });
  uint32_t got = sig_recvd_ & sigs;
  sig_recvd_ &= ~sigs;
  return got;
}

void Hle::put_msg(uint32_t port, uint32_t msg) {
  m.w8(msg + LN_TYPE, NT_MESSAGE);
  add_tail(port + MP_MSGLIST, msg);
  if ((m.r8(port + MP_FLAGS) & 3) == 0)  // PA_SIGNAL
    signal(m.r32(port + MP_SIGTASK), 1u << m.r8(port + MP_SIGBIT));
}

void Hle::reply_msg(uint32_t msg) {
  uint32_t port = m.r32(msg + MN_REPLYPORT);
  if (!port) { m.w8(msg + LN_TYPE, NT_REPLYMSG); return; }
  put_msg(port, msg);
  m.w8(msg + LN_TYPE, NT_REPLYMSG);
}

// ---- interrupts --------------------------------------------------------------

bool Hle::interrupts_disabled() const { return disable_ > 0 || m.cpu.imask >= 3; }

void Hle::interrupt(int level) {
  if (level <= cur_level_) return;
  int saved = cur_level_;
  cur_level_ = level;
  uint32_t exec = libs_[LIB_EXEC];
  uint16_t pending = m.cs_.intena_ & m.cs_.intreq_;
  for (int bit = 0; bit < 14; ++bit) {
    static const int lvl[14] = {1, 1, 1, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6};
    if (lvl[bit] != level || !(pending & (1 << bit))) continue;
    uint32_t d[8] = {}, a[8] = {};
    d[1] = pending;
    a[0] = 0xDFF000;
    a[6] = exec;
    if (vectors_[bit]) {
      uint32_t is = vectors_[bit];
      a[1] = m.r32(is + IS_DATA);
      a[5] = m.r32(is + IS_CODE);
      m.call68k(a[5], d, a);  // handler clears INTREQ itself
      continue;
    }
    m.cs_.intreq_ &= uint16_t(~(1 << bit));
    for (uint32_t is : servers_[bit]) {
      a[1] = m.r32(is + IS_DATA);
      a[5] = m.r32(is + IS_CODE);
      m.call68k(a[5], d, a);
      if (!m.last_call_z()) break;
    }
  }
  cur_level_ = saved;
}

void Hle::line_tick(int vpos) {
  (void)vpos;
  if (!timers_.empty()) timer_pump();
}

void Hle::frame_tick() {
  kbd_pump();
  gameport_pump();
}

// ---- input --------------------------------------------------------------------

void Hle::key_event(uint8_t rawkey, bool down) {
  if (rawkey < 0x80) {
    if (down) key_matrix_[rawkey >> 3] |= uint8_t(1 << (rawkey & 7));
    else key_matrix_[rawkey >> 3] &= uint8_t(~(1 << (rawkey & 7)));
  }
  static const struct { uint8_t key; uint16_t q; } quals[] = {
      {0x60, 1}, {0x61, 2}, {0x63, 8}, {0x64, 0x10}, {0x65, 0x20}, {0x66, 0x40}, {0x67, 0x80}};
  for (auto& q : quals)
    if (q.key == rawkey) { if (down) qualifier_ |= q.q; else qualifier_ &= uint16_t(~q.q); }
  if (kbd_queue_.size() < 64) kbd_queue_.push_back({rawkey, down, qualifier_});
}

void Hle::mouse_button(int button, bool down) { (void)button; (void)down; }

void Hle::joy_fire(bool down) {
  if (fire_queue_.size() < 16) fire_queue_.push_back(down);
}

void Hle::write_input_event(uint32_t buf, uint8_t cls, uint16_t code, uint16_t qual,
                            int16_t x, int16_t y) {
  m.w32(buf + 0, 0);
  m.w8(buf + 4, cls);
  m.w8(buf + 5, 0);
  m.w16(buf + 6, code);
  m.w16(buf + 8, qual);
  m.w16(buf + 10, uint16_t(x));
  m.w16(buf + 12, uint16_t(y));
  uint64_t us = micros();
  m.w32(buf + 14, uint32_t(us / 1000000));
  m.w32(buf + 18, uint32_t(us % 1000000));
}

void Hle::kbd_pump() {
  while (!kbd_queue_.empty() && !kbd_reads_.empty()) {
    uint32_t io = kbd_reads_.front();
    kbd_reads_.pop_front();
    KeyEv ev = kbd_queue_.front();
    kbd_queue_.pop_front();
    write_input_event(m.r32(io + IO_DATA), 1, uint16_t(ev.key | (ev.down ? 0 : 0x80)), ev.qual, 0, 0);
    m.w32(io + IO_ACTUAL, 22);
    complete_io(io);
  }
}

void Hle::gameport_pump() {
  while (!fire_queue_.empty() && !gameport_reads_.empty()) {
    uint32_t io = gameport_reads_.front();
    gameport_reads_.pop_front();
    bool down = fire_queue_.front();
    fire_queue_.pop_front();
    write_input_event(m.r32(io + IO_DATA), 2, down ? 0x68 : 0xE8, 0, 0, 0);
    m.w32(io + IO_ACTUAL, 22);
    complete_io(io);
  }
}

uint64_t Hle::micros() const { return m.micros(); }

void Hle::timer_pump() {
  uint64_t now = micros();
  for (size_t i = 0; i < timers_.size();) {
    if (timers_[i].due <= now) {
      uint32_t io = timers_[i].io;
      timers_.erase(timers_.begin() + long(i));
      complete_io(io);
    } else {
      ++i;
    }
  }
}

// ---- IO requests ----------------------------------------------------------

int Hle::lib_for_device(uint32_t dev) const {
  for (int i = LIB_INPUT; i <= LIB_AUDIO; ++i)
    if (libs_[i] == dev) return i;
  return -1;
}

void Hle::complete_io(uint32_t io, int8_t error) {
  m.w8(io + IO_ERROR, uint8_t(error));
  if (m.r8(io + IO_FLAGS) & IOF_QUICK) {
    // Completed inside DoIO/BeginIO with IOF_QUICK: no reply needed if still
    // inside the call; otherwise reply normally.
    m.w8(io + IO_FLAGS, m.r8(io + IO_FLAGS) & ~IOF_QUICK);
  }
  reply_msg(io);
}

void Hle::abort_io(uint32_t io) {
  auto drop = [&](std::deque<uint32_t>& q) {
    auto it = std::find(q.begin(), q.end(), io);
    if (it != q.end()) { q.erase(it); return true; }
    return false;
  };
  bool found = drop(kbd_reads_) || drop(gameport_reads_);
  for (size_t i = 0; i < timers_.size(); ++i)
    if (timers_[i].io == io) { timers_.erase(timers_.begin() + long(i)); found = true; break; }
  if (found) complete_io(io, IOERR_ABORTED);
}

void Hle::begin_io(uint32_t io) {
  int lib = lib_for_device(m.r32(io + IO_DEVICE));
  uint16_t cmd = m.r16(io + IO_COMMAND);
  m.w8(io + LN_TYPE, NT_MESSAGE);
  m.w8(io + IO_ERROR, 0);
  switch (lib) {
    case LIB_KEYBOARD:
      if (cmd == 9) {  // KBD_READEVENT
        kbd_reads_.push_back(io);
        kbd_pump();
        return;
      }
      if (cmd == 10) {  // KBD_READMATRIX
        uint32_t len = std::min<uint32_t>(m.r32(io + IO_LENGTH), 16);
        for (uint32_t i = 0; i < len; ++i) m.w8(m.r32(io + IO_DATA) + i, key_matrix_[i]);
        m.w32(io + IO_ACTUAL, len);
      }
      complete_io(io);
      return;
    case LIB_GAMEPORT:
      switch (cmd) {
        case 9:  // GPD_READEVENT
          gameport_reads_.push_back(io);
          gameport_pump();
          return;
        case 10: m.w8(m.r32(io + IO_DATA), gameport_type_); break;   // GPD_ASKCTYPE
        case 11: gameport_type_ = m.r8(m.r32(io + IO_DATA)); break;  // GPD_SETCTYPE
        default: break;
      }
      complete_io(io);
      return;
    case LIB_TIMER:
      if (cmd == 10) {  // TR_GETSYSTIME
        uint64_t us = micros();
        m.w32(io + 32, uint32_t(us / 1000000));
        m.w32(io + 36, uint32_t(us % 1000000));
        complete_io(io);
        return;
      }
      if (cmd == 9) {  // TR_ADDREQUEST
        uint64_t d = uint64_t(m.r32(io + 32)) * 1000000 + m.r32(io + 36);
        timers_.push_back({io, micros() + d});
        return;
      }
      complete_io(io);
      return;
    case LIB_INPUT:
      if (cmd == 9) input_handlers_.push_back(m.r32(io + IO_DATA));  // IND_ADDHANDLER
      if (cmd == 10) {
        auto it = std::find(input_handlers_.begin(), input_handlers_.end(), m.r32(io + IO_DATA));
        if (it != input_handlers_.end()) input_handlers_.erase(it);
      }
      complete_io(io);
      return;
    case LIB_AUDIO:
      if (m.verbose) m.log("[audio.device] cmd %d\n", cmd);
      complete_io(io);
      return;
    default:
      m.log("BeginIO on unknown device %06x cmd %d\n", m.r32(io + IO_DEVICE), cmd);
      complete_io(io, IOERR_NOCMD);
      return;
  }
}

void Hle::device(int lib, int lvo) {
  switch (lvo) {
    case 30: begin_io(A(1)); return;  // BeginIO
    case 36: abort_io(A(1)); return;  // AbortIO
    case 42:  // timer: AddTime etc. are not used
    default: unknown(lib, lvo); return;
  }
}

void Hle::resource(int lvo) {
  switch (lvo) {
    case 6: return;   // AllocPotBits: d0 = bits granted (unchanged)
    case 12: return;  // FreePotBits
    case 18:          // WritePotgo(word d0, mask d1)
      m.custom_write(0x034, uint16_t(D(0) & D(1)));
      return;
    default: unknown(LIB_RESOURCE, lvo); return;
  }
}

// ---- exec -----------------------------------------------------------------------

void Hle::exec(int lvo) {
  switch (lvo) {
    case 30: {  // Supervisor(a5 userFunc): run with S set, returns via RTE
      m.log("Supervisor() called\n");
      return;
    }
    case 120: ++disable_; return;  // Disable
    case 126: if (disable_ > 0) --disable_; if (!disable_) m.check_interrupts(); return;
    case 132: ++forbid_; return;   // Forbid
    case 138: if (forbid_ > 0) --forbid_; return;
    case 144: {  // SetSR(d0 new, d1 mask)
      uint16_t old = m.cpu.sr();
      m.cpu.set_ccr(uint16_t((old & ~D(1)) | (D(0) & D(1))));
      D(0) = old;
      return;
    }
    case 150: D(0) = SSP_TOP; return;  // SuperState
    case 156: return;                  // UserState
    case 162: {  // SetIntVector(d0 num, a1 is) → old
      int n = int(D(0) & 15);
      uint32_t old = vectors_[n];
      vectors_[n] = A(1);
      D(0) = old;
      return;
    }
    case 168: {  // AddIntServer(d0 num, a1 is)
      int n = int(D(0) & 15);
      auto& v = servers_[n];
      int8_t pri = int8_t(m.r8(A(1) + LN_PRI));
      auto it = v.begin();
      while (it != v.end() && int8_t(m.r8(*it + LN_PRI)) >= pri) ++it;
      v.insert(it, A(1));
      m.cs_.intena_ |= uint16_t((1 << n) | INT_INTEN);
      return;
    }
    case 174: {  // RemIntServer
      auto& v = servers_[D(0) & 15];
      v.erase(std::remove(v.begin(), v.end(), A(1)), v.end());
      return;
    }
    case 180: {  // Cause(a1 is)
      uint32_t is = A(1);
      uint32_t d[8] = {}, a[8] = {};
      a[1] = m.r32(is + IS_DATA);
      a[5] = m.r32(is + IS_CODE);
      a[6] = libs_[LIB_EXEC];
      m.call68k(a[5], d, a);
      return;
    }
    case 198: {  // AllocMem(d0 size, d1 req)
      uint32_t size = D(0);
      D(0) = size ? m.alloc(size, true) : 0;
      if (m.verbose) m.log("AllocMem(%u, %x) = %06x\n", size, D(1), D(0));
      return;
    }
    case 204: D(0) = 0; return;  // AllocAbs
    case 210: m.free(A(1), D(0)); return;  // FreeMem
    case 216: {  // AvailMem(d1 req)
      uint32_t req = D(1);
      if (req & 0x20000) {  // MEMF_LARGEST
        uint32_t best = 0;
        for (auto& [a, s] : m.free_list_) best = std::max(best, s);
        D(0) = best;
      } else {
        D(0) = m.avail();
      }
      return;
    }
    case 234: {  // Insert(a0 list, a1 node, a2 pred)
      if (!A(2)) { add_head(A(0), A(1)); return; }
      uint32_t succ = m.r32(A(2) + LN_SUCC);
      if (!succ) { add_tail(A(0), A(1)); return; }
      m.w32(A(1) + LN_SUCC, succ);
      m.w32(A(1) + LN_PRED, A(2));
      m.w32(succ + LN_PRED, A(1));
      m.w32(A(2) + LN_SUCC, A(1));
      return;
    }
    case 240: add_head(A(0), A(1)); return;
    case 246: add_tail(A(0), A(1)); return;
    case 252: remove(A(1)); return;
    case 258: D(0) = rem_head(A(0)); return;
    case 264: {  // RemTail
      uint32_t tp = m.r32(A(0) + LH_TAILPRED);
      if (m.r32(tp + LN_PRED) == 0) { D(0) = 0; return; }
      remove(tp);
      D(0) = tp;
      return;
    }
    case 270: enqueue(A(0), A(1)); return;
    case 276: {  // FindName(a0 list, a1 name)
      std::string name = m.cstr(A(1));
      uint32_t n = m.r32(A(0) + LH_HEAD);
      D(0) = 0;
      while (m.r32(n + LN_SUCC)) {
        if (m.cstr(m.r32(n + LN_NAME)) == name) { D(0) = n; return; }
        n = m.r32(n + LN_SUCC);
      }
      return;
    }
    case 294: {  // FindTask(a1 name)
      if (!A(1)) { D(0) = task_; return; }
      std::string name = m.cstr(A(1));
      D(0) = name == "input.device" ? input_task_ : 0;
      return;
    }
    case 300: {  // SetTaskPri(a1 task, d0 pri)
      int8_t old = int8_t(m.r8(A(1) + LN_PRI));
      m.w8(A(1) + LN_PRI, uint8_t(D(0)));
      D(0) = uint32_t(int32_t(old));
      return;
    }
    case 306: {  // SetSignal(d0 new, d1 mask)
      uint32_t old = sig_recvd_;
      sig_recvd_ = (old & ~D(1)) | (D(0) & D(1));
      D(0) = old;
      return;
    }
    case 318: D(0) = wait(D(0)); return;      // Wait
    case 324: signal(A(1), D(0)); return;      // Signal
    case 330: {  // AllocSignal(d0 num)
      int8_t want = int8_t(D(0));
      int got = -1;
      if (want >= 0) { if (!(sig_alloc_ & (1u << want))) got = want; }
      else for (int i = 31; i >= 16; --i) if (!(sig_alloc_ & (1u << i))) { got = i; break; }
      if (got >= 0) { sig_alloc_ |= 1u << got; sig_recvd_ &= ~(1u << got); }
      D(0) = uint32_t(got);
      return;
    }
    case 336: {  // FreeSignal(d0)
      int8_t s = int8_t(D(0));
      if (s >= 0) sig_alloc_ &= ~(1u << s);
      return;
    }
    case 354: return;  // AddPort
    case 360: return;  // RemPort
    case 366: put_msg(A(0), A(1)); return;  // PutMsg
    case 372: D(0) = rem_head(A(0) + MP_MSGLIST); return;  // GetMsg
    case 378: reply_msg(A(1)); return;  // ReplyMsg
    case 384: {  // WaitPort(a0)
      uint32_t port = A(0);
      m.idle_until([&] { return m.r32(m.r32(port + MP_MSGLIST + LH_HEAD) + LN_SUCC) != 0; });
      D(0) = m.r32(port + MP_MSGLIST + LH_HEAD);
      return;
    }
    case 390: D(0) = 0; return;  // FindPort
    case 408:                    // OldOpenLibrary
    case 552: {                  // OpenLibrary(a1 name, d0 version)
      std::string name = m.cstr(A(1));
      D(0) = 0;
      for (int i = 0; i < LIB_COUNT; ++i)
        if (name == m.cstr(m.r32(libs_[i] + LN_NAME))) D(0) = libs_[i];
      if (!D(0)) m.log("OpenLibrary(%s) failed\n", name.c_str());
      return;
    }
    case 414: return;  // CloseLibrary
    case 444: {        // OpenDevice(a0 name, d0 unit, a1 io, d1 flags)
      std::string name = m.cstr(A(0));
      uint32_t io = A(1);
      int lib = -1;
      for (int i = LIB_INPUT; i <= LIB_AUDIO; ++i)
        if (name == m.cstr(m.r32(libs_[i] + LN_NAME))) lib = i;
      if (lib < 0) {
        m.log("OpenDevice(%s) failed\n", name.c_str());
        m.w8(io + IO_ERROR, uint8_t(IOERR_OPENFAIL));
        D(0) = uint32_t(int32_t(IOERR_OPENFAIL));
        return;
      }
      m.w32(io + IO_DEVICE, libs_[lib]);
      m.w32(io + IO_UNIT, D(0));
      m.w8(io + IO_ERROR, 0);
      if (lib == LIB_AUDIO) {
        // Allocation at open: grant the first requested channel combination.
        uint32_t data = m.r32(io + 34), len = m.r32(io + 38);
        uint8_t combo = len ? m.r8(data) : 0;
        m.w32(io + IO_UNIT, combo);
        m.w16(io + 32, 1);  // ioa_AllocKey
      }
      D(0) = 0;
      return;
    }
    case 450: {  // CloseDevice(a1)
      abort_io(A(1));
      return;
    }
    case 456: {  // DoIO(a1)
      uint32_t io = A(1);
      m.w8(io + IO_FLAGS, IOF_QUICK);
      begin_io(io);
      m.idle_until([&] { return m.r8(io + LN_TYPE) == NT_REPLYMSG; });
      remove(io);
      D(0) = uint32_t(int32_t(int8_t(m.r8(io + IO_ERROR))));
      return;
    }
    case 462: {  // SendIO(a1)
      m.w8(A(1) + IO_FLAGS, 0);
      begin_io(A(1));
      return;
    }
    case 468: {  // CheckIO(a1)
      D(0) = m.r8(A(1) + LN_TYPE) == NT_REPLYMSG ? A(1) : 0;
      return;
    }
    case 474: {  // WaitIO(a1)
      uint32_t io = A(1);
      m.idle_until([&] { return m.r8(io + LN_TYPE) == NT_REPLYMSG; });
      remove(io);
      D(0) = uint32_t(int32_t(int8_t(m.r8(io + IO_ERROR))));
      return;
    }
    case 480: abort_io(A(1)); return;  // AbortIO
    case 498: {  // OpenResource(a1 name)
      if (m.verbose) m.log("OpenResource(%s)\n", m.cstr(A(1)).c_str());
      D(0) = libs_[LIB_RESOURCE];
      return;
    }
    case 528: D(0) = m.cpu.ccr(); return;  // GetCC
    case 534: D(0) = 3; return;            // TypeOfMem: CHIP|PUBLIC
    case 624:                               // CopyMem(a0 src, a1 dst, d0 size)
    case 630:
      for (uint32_t i = 0; i < D(0); ++i) m.w8(A(1) + i, m.r8(A(0) + i));
      return;
    default: unknown(LIB_EXEC, lvo); return;
  }
}

// ---- dos ----------------------------------------------------------------------

std::string Hle::host_path(const std::string& amiga_name, bool for_write) {
  std::string n = amiga_name;
  auto colon = n.find(':');
  if (colon != std::string::npos) n = n.substr(colon + 1);
  // Case-insensitive lookup below `root`.  When data is an ADF, only the
  // save directory is a real filesystem tree.
  auto resolve = [&](const std::string& root) -> std::string {
    fs::path p = root;
    size_t start = 0;
    while (start <= n.size()) {
      size_t slash = n.find('/', start);
      std::string comp = n.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
      start = slash == std::string::npos ? n.size() + 1 : slash + 1;
      if (comp.empty()) continue;
      fs::path next = p / comp;
      if (!fs::exists(next) && fs::is_directory(p)) {
        for (auto& e : fs::directory_iterator(p)) {
          std::string a = e.path().filename().string(), b = comp;
          std::transform(a.begin(), a.end(), a.begin(), ::tolower);
          std::transform(b.begin(), b.end(), b.begin(), ::tolower);
          if (a == b) { next = e.path(); break; }
        }
      }
      p = next;
    }
    return p.string();
  };
  std::string saved = resolve(m.save_dir);
  if (for_write) {
    fs::create_directories(fs::path(saved).parent_path());
    return saved;
  }
  if (fs::exists(saved)) return saved;
  if (m.data_is_adf) return {};  // no host path; caller falls back to the ADF
  return resolve(m.data_dir);
}

uint32_t Hle::dos_open(const std::string& name, int mode) {
  bool write = mode == 1006;
  std::string path = host_path(name, write);
  FILE* f = nullptr;
  bool from_adf = false;
  std::vector<uint8_t>* adf_buf = nullptr;

  if (mode == 1006) {
    f = fopen(path.c_str(), "w+b");
  } else if (mode == 1005) {
    if (!path.empty()) f = fopen(path.c_str(), "rb");
    if (!f && m.adf()) {
      auto bytes = m.disk_read(name);
      if (!bytes.empty() || m.disk_has(name)) {
        adf_buf = new std::vector<uint8_t>(std::move(bytes));
        size_t sz = adf_buf->size();
        // fmemopen requires a non-null buffer; use a one-byte dummy for empty files.
        if (sz == 0) adf_buf->push_back(0);
        f = fmemopen(adf_buf->data(), sz, "rb");
        if (f) {
          from_adf = true;
          path = std::string("adf:") + name;
        } else {
          delete adf_buf;
          adf_buf = nullptr;
        }
      }
    }
  } else {  // MODE_READWRITE
    std::string wp = host_path(name, true);
    if (!path.empty() && fs::exists(path) && path != wp && !fs::exists(wp))
      fs::copy_file(path, wp);
    // Seed the save-dir copy from the ADF when the host has no original.
    if (!fs::exists(wp) && m.adf() && m.disk_has(name)) {
      auto bytes = m.disk_read(name);
      fs::create_directories(fs::path(wp).parent_path());
      FILE* out = fopen(wp.c_str(), "w+b");
      if (out) {
        if (!bytes.empty()) fwrite(bytes.data(), 1, bytes.size(), out);
        fclose(out);
      }
    }
    f = fopen(wp.c_str(), "r+b");
    if (!f) f = fopen(wp.c_str(), "w+b");
    path = wp;
  }
  if (!f) {
    if (m.verbose) m.log("Open(%s) failed (%s)\n", name.c_str(), path.c_str());
    ioerr_ = 205;
    return 0;
  }
  uint32_t h = next_handle_++;
  files_[h] = {f, path, name, mode != 1005, adf_buf};
  if (m.verbose)
    m.log("Open(%s, %d) = %u%s\n", name.c_str(), mode, h, from_adf ? " [adf]" : "");
  return h;
}

// MODE_OLDFILE handles are read/write on AmigaDOS. They are opened read-only
// (possibly from the data dir / ADF); the first Write copies the file into the
// save dir and reopens it there at the same position.
bool Hle::make_writable(File& file) {
  if (file.writable) return true;
  long pos = ftell(file.f);
  std::string wp = host_path(file.name, true);
  if (file.adf_buf) {
    // Materialise the ADF contents into the save directory.
    fs::create_directories(fs::path(wp).parent_path());
    FILE* out = fopen(wp.c_str(), "w+b");
    if (!out) return false;
    // adf_buf may hold a dummy byte for empty files; size was the original.
    size_t n = file.adf_buf->size();
    // We stored original size implicitly: if only the dummy was added, n==1 and
    // the open used size 0.  Reconstruct via disk_size when possible.
    int32_t orig = m.disk_size(file.name);
    if (orig >= 0) n = size_t(orig);
    if (n) fwrite(file.adf_buf->data(), 1, n, out);
    fclose(out);
    delete file.adf_buf;
    file.adf_buf = nullptr;
  } else if (!file.path.empty() && wp != file.path && fs::exists(file.path)) {
    std::error_code ec;
    fs::copy_file(file.path, wp, fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
  }
  FILE* f = fopen(wp.c_str(), "r+b");
  if (!f) return false;
  fclose(file.f);
  file.f = f;
  file.path = wp;
  file.writable = true;
  fseek(f, pos, SEEK_SET);
  return true;
}

void Hle::dos(int lvo) {
  switch (lvo) {
    case 30: D(0) = dos_open(m.cstr(D(1)), int(D(2))); return;  // Open
    case 36: {  // Close
      auto it = files_.find(D(1));
      if (it != files_.end()) {
        fclose(it->second.f);
        delete it->second.adf_buf;
        files_.erase(it);
      }
      D(0) = 1;
      return;
    }
    case 42: case 48: {  // Read / Write
      auto it = files_.find(D(1));
      if (it == files_.end()) {
        if (lvo == 48 && D(1) == 0xFFFF) {  // Output()
          for (uint32_t i = 0; i < D(3); ++i) fputc(m.r8(D(2) + i), stdout);
          D(0) = D(3);
          return;
        }
        D(0) = uint32_t(-1);
        return;
      }
      std::vector<uint8_t> buf(D(3));
      size_t n;
      if (lvo == 42) {
        n = fread(buf.data(), 1, buf.size(), it->second.f);
        for (size_t i = 0; i < n; ++i) m.w8(D(2) + uint32_t(i), buf[i]);
      } else {
        for (size_t i = 0; i < buf.size(); ++i) buf[i] = m.r8(D(2) + uint32_t(i));
        if (!make_writable(it->second)) { D(0) = uint32_t(-1); return; }
        n = fwrite(buf.data(), 1, buf.size(), it->second.f);
        fflush(it->second.f);
      }
      D(0) = uint32_t(n);
      return;
    }
    case 54: D(0) = 0xFFFE; return;  // Input
    case 60: D(0) = 0xFFFF; return;  // Output
    case 66: {  // Seek(d1 fh, d2 pos, d3 mode)
      auto it = files_.find(D(1));
      if (it == files_.end()) { D(0) = uint32_t(-1); return; }
      long old = ftell(it->second.f);
      int whence = int32_t(D(3)) < 0 ? SEEK_SET : D(3) == 0 ? SEEK_CUR : SEEK_END;
      if (fseek(it->second.f, long(int32_t(D(2))), whence) != 0) { D(0) = uint32_t(-1); return; }
      D(0) = uint32_t(old);
      return;
    }
    case 72: {  // DeleteFile
      std::string p = host_path(m.cstr(D(1)), true);
      std::error_code ec;
      D(0) = fs::remove(p, ec) ? 1 : 0;
      if (!D(0)) ioerr_ = 205;
      return;
    }
    case 78: {  // Rename
      std::string a = host_path(m.cstr(D(1)), false), b = host_path(m.cstr(D(2)), true);
      std::error_code ec;
      fs::rename(a, b, ec);
      D(0) = ec ? 0 : 1;
      return;
    }
    case 84: {  // Lock(d1 name, d2 mode)
      std::string name = m.cstr(D(1));
      std::string p = host_path(name, false);
      if (p.empty() || !fs::exists(p)) {
        // ADF-resident file/dir, or a bare device/volume ("df0:").
        if (m.adf() && (m.disk_has(name) || m.adf()->is_dir(name))) {
          p = std::string("adf:") + name;
        } else if (!name.empty() && name.back() == ':') {
          p = m.save_dir;
        } else {
          ioerr_ = 205; D(0) = 0; return;
        }
      }
      uint32_t h = next_handle_++;
      locks_[h] = p;
      D(0) = h;
      return;
    }
    case 90: locks_.erase(D(1)); return;  // UnLock
    case 96: {  // DupLock
      auto it = locks_.find(D(1));
      if (it == locks_.end()) { D(0) = 0; return; }
      uint32_t h = next_handle_++;
      locks_[h] = it->second;
      D(0) = h;
      return;
    }
    case 102: {  // Examine(d1 lock, d2 fib)
      auto it = locks_.find(D(1));
      if (it == locks_.end()) { D(0) = 0; return; }
      uint32_t fib = D(2);
      for (uint32_t i = 0; i < 260; ++i) m.w8(fib + i, 0);
      std::string lock_path = it->second;
      bool dir = false;
      uint32_t size = 0;
      std::string fn;
      if (lock_path.rfind("adf:", 0) == 0) {
        std::string aname = lock_path.substr(4);
        dir = m.adf() && m.adf()->is_dir(aname);
        int32_t sz = m.disk_size(aname);
        size = dir || sz < 0 ? 0 : uint32_t(sz);
        auto slash = aname.find_last_of('/');
        fn = (slash == std::string::npos ? aname : aname.substr(slash + 1)).substr(0, 106);
      } else {
        fs::path p = lock_path;
        dir = fs::is_directory(p);
        fn = p.filename().string().substr(0, 106);
        size = dir ? 0 : uint32_t(fs::file_size(p));
      }
      m.w32(fib + 4, dir ? 2 : uint32_t(-3));
      m.w8(fib + 8, 0);
      for (size_t i = 0; i < fn.size(); ++i) m.w8(fib + 8 + uint32_t(i), uint8_t(fn[i]));
      m.w32(fib + 120, dir ? 2 : uint32_t(-3));
      m.w32(fib + 124, size);
      m.w32(fib + 128, (size + 487) / 488);
      D(0) = 1;
      return;
    }
    case 108: D(0) = 0; ioerr_ = 232; return;  // ExNext: no more entries
    case 114: {  // Info(d1 lock, d2 infodata)
      uint32_t id = D(2);
      for (uint32_t i = 0; i < 36; ++i) m.w8(id + i, 0);
      m.w32(id + 8, 82);         // ID_VALIDATED (write enabled)
      m.w32(id + 12, 1758);      // blocks
      m.w32(id + 16, 900);
      m.w32(id + 20, 488);
      m.w32(id + 24, 0x444F5300);  // DOS\0
      m.w32(id + 28, 1);
      D(0) = 1;
      return;
    }
    case 126: D(0) = 0; return;   // CurrentDir
    case 132: D(0) = ioerr_; return;  // IoErr
    case 198: {  // Delay(d1 ticks @50Hz)
      uint64_t until = micros() + uint64_t(D(1)) * 20000;
      m.idle_until([&] { return micros() >= until; });
      return;
    }
    default: unknown(LIB_DOS, lvo); return;
  }
}

// ---- graphics -----------------------------------------------------------------

void Hle::build_copper(uint32_t view) {
  // Reuse the view's current list if we built it, else make a new one
  // (the game clears LOFCprList to get a second list for double buffering).
  uint32_t hdr = m.r32(view + 4);
  if (!cprlists_.count(hdr)) {
    hdr = m.alloc(4096 + 16);
    cprlists_.insert(hdr);
  }
  uint32_t start = hdr + 16;
  uint32_t p = start;
  auto move = [&](uint16_t reg, uint16_t val) { m.w16(p, reg); m.w16(p + 2, val); p += 4; };
  auto waitl = [&](int line) {
    if (line < 0) line = 0;
    if (line > 255) line = 255;
    m.w16(p, uint16_t((line << 8) | 0x07 | 1));
    m.w16(p + 2, 0xFFFE);
    p += 4;
  };
  int vdy = int16_t(m.r16(view + 12)), vdx = int16_t(m.r16(view + 14));
  // Collect visible viewports sorted by vertical position.
  std::vector<uint32_t> vps;
  for (uint32_t vp = m.r32(view + 0); vp && vps.size() < 16; vp = m.r32(vp + 0))
    if (!(m.r16(vp + 32) & 0x2000)) vps.push_back(vp);
  std::sort(vps.begin(), vps.end(), [&](uint32_t a, uint32_t b) {
    return int16_t(m.r16(a + 30)) < int16_t(m.r16(b + 30));
  });
  int last_end = 0;
  move(0x100, 0x0200);
  for (uint32_t vp : vps) {
    uint32_t ras = m.r32(vp + 36);
    if (!ras) continue;
    uint32_t bm = m.r32(ras + 4);
    if (!bm) continue;
    uint16_t modes = m.r16(vp + 32);
    bool hires = modes & 0x8000;
    bool dpf = modes & 0x0400;
    int depth = m.r8(bm + 5);
    uint32_t ras2 = dpf ? m.r32(ras + 0) : 0;
    uint32_t bm2 = ras2 ? m.r32(ras2 + 4) : 0;
    if (dpf && bm2) depth += m.r8(bm2 + 5);
    int dw = int16_t(m.r16(vp + 24)), dh = int16_t(m.r16(vp + 26));
    int vstart = vdy + int16_t(m.r16(vp + 30));
    int hstart = vdx + int16_t(m.r16(vp + 28));
    int lores_w = hires ? dw / 2 : dw;
    waitl(std::max(last_end, vstart - 1));
    uint16_t con0 = uint16_t((depth & 7) << 12 | 0x0200 | (modes & (0x8000 | 0x0800 | 0x0400 | 0x0004)));
    int ddfstrt = hires ? ((hstart - 9) / 2) & 0xFC : ((hstart - 17) / 2) & 0xFC;
    int nwords = (dw + 15) / 16;
    int ddfstop = hires ? ddfstrt + (nwords - 2) * 4 : ddfstrt + (nwords - 1) * 8;
    move(0x08E, uint16_t(((vstart & 0xFF) << 8) | (hstart & 0xFF)));
    move(0x090, uint16_t((((vstart + dh) & 0xFF) << 8) | ((hstart + lores_w) & 0xFF)));
    move(0x092, uint16_t(ddfstrt));
    move(0x094, uint16_t(ddfstop));
    move(0x102, 0);
    move(0x104, 0x24);
    auto planes = [&](uint32_t r, uint32_t b, int first, int stride, uint16_t modreg) {
      int bpr = m.r16(b + 0);
      int d = m.r8(b + 5);
      int rx = int16_t(m.r16(r + 8)), ry = int16_t(m.r16(r + 10));
      for (int i = 0; i < d; ++i) {
        uint32_t pl = m.r32(b + 8 + uint32_t(i) * 4) + uint32_t(ry * bpr + (rx >> 4) * 2);
        int idx = first + i * stride;
        if (idx > 5) break;
        move(uint16_t(0x0E0 + idx * 4), uint16_t(pl >> 16));
        move(uint16_t(0x0E2 + idx * 4), uint16_t(pl));
      }
      move(modreg, uint16_t(bpr - nwords * 2));
    };
    if (dpf && bm2) {
      planes(ras, bm, 0, 2, 0x108);
      planes(ras2, bm2, 1, 2, 0x10A);
    } else {
      planes(ras, bm, 0, 1, 0x108);
      move(0x10A, uint16_t(m.r16(bm + 0) - nwords * 2));
    }
    uint32_t cm = m.r32(vp + 4);
    std::vector<uint32_t> moves;
    if (cm) {
      int count = std::min<int>(m.r16(cm + 2), 32);
      uint32_t table = m.r32(cm + 4);
      for (int i = 0; i < count; ++i) {
        moves.push_back(p);
        move(uint16_t(0x180 + i * 2), m.r16(table + uint32_t(i) * 2));
      }
    }
    if (uint32_t dsp = m.r32(vp + 8)) color_moves_[dsp] = moves;
    move(0x100, con0);
    last_end = vstart + dh;
    waitl(last_end);
    move(0x100, 0x0200);
  }
  m.w16(p, 0xFFFF);
  m.w16(p + 2, 0xFFFE);
  p += 4;
  // cprlist header: Next, start, MaxCount
  m.w32(hdr + 0, 0);
  m.w32(hdr + 4, start);
  m.w16(hdr + 8, uint16_t((p - start) / 4));
  m.w32(view + 4, hdr);  // LOFCprList
  if (view == active_view_) m.cs_.cop1lc_ = start;
}

void Hle::poke_colors(uint32_t vp) {
  // Like LoadRGB4 in Kickstart 1.3: update the colour MOVEs of the copper
  // list that belongs to the viewport's current DspIns.
  auto it = color_moves_.find(m.r32(vp + 8));
  uint32_t cm = m.r32(vp + 4);
  if (it == color_moves_.end() || !cm) return;
  uint32_t table = m.r32(cm + 4);
  for (size_t i = 0; i < it->second.size(); ++i)
    m.w16(it->second[i] + 2, m.r16(table + uint32_t(i) * 2));
}

void Hle::gfx(int lvo) {
  uint32_t gfxbase = libs_[LIB_GFX];
  switch (lvo) {
    case 192: {  // LoadRGB4(a0 vp, a1 colors, d0 count)
      uint32_t vp = A(0), cm = m.r32(vp + 4);
      if (!cm) return;
      int count = std::min<int>(int(D(0) & 0xFFFF), m.r16(cm + 2));
      uint32_t table = m.r32(cm + 4);
      for (int i = 0; i < count; ++i)
        m.w16(table + uint32_t(i) * 2, m.r16(A(1) + uint32_t(i) * 2) & 0xFFF);
      poke_colors(vp);
      return;
    }
    case 198: {  // InitRastPort(a1)
      uint32_t rp = A(1);
      for (uint32_t i = 0; i < 100; ++i) m.w8(rp + i, 0);
      m.w8(rp + 24, 0xFF);   // Mask
      m.w8(rp + 25, 0xFF);   // FgPen
      m.w8(rp + 27, 0xFF);   // AOlPen
      m.w8(rp + 28, 1);      // DrawMode JAM2
      m.w16(rp + 34, 0xFFFF);  // LinePtrn
      return;
    }
    case 204: {  // InitVPort(a0)
      for (uint32_t i = 0; i < 40; ++i) m.w8(A(0) + i, 0);
      return;
    }
    case 210: build_copper(A(1)); return;  // MrgCop(a1 view)
    case 216: {  // MakeVPort(a0 view, a1 vp): just provide a DspIns token
      if (!m.r32(A(1) + 8)) m.w32(A(1) + 8, m.alloc(16));
      return;
    }
    case 222: {  // LoadView(a1 view)
      ++m.loadview_count;
      active_view_ = A(1);
      m.w32(gfxbase + 34, active_view_);
      if (!active_view_) { m.cs_.cop1lc_ = 0; m.cs_.bplcon0_ = 0x200; return; }
      uint32_t cpr = m.r32(active_view_ + 4);
      if (!cprlists_.count(cpr)) build_copper(active_view_), cpr = m.r32(active_view_ + 4);
      m.cs_.cop1lc_ = m.r32(cpr + 4);
      m.w32(gfxbase + 50, m.cs_.cop1lc_);  // LOFlist
      m.cs_.dmacon_ |= 0x0200 | 0x0100 | 0x0080;
      return;
    }
    case 228: return;  // WaitBlit
    case 270: {        // WaitTOF
      uint64_t f = m.frame_count_;
      m.idle_until([&] { return m.frame_count_ != f; });
      return;
    }
    case 288: {  // SetRGB4(a0 vp, d0 n, d1 r, d2 g, d3 b)
      uint32_t cm = m.r32(A(0) + 4);
      if (cm && D(0) < m.r16(cm + 2)) {
        uint16_t c = uint16_t((D(1) & 15) << 8 | (D(2) & 15) << 4 | (D(3) & 15));
        m.w16(m.r32(cm + 4) + D(0) * 2, c);
        poke_colors(A(0));
      }
      return;
    }
    case 300: {  // BltClear(a1 mem, d0 bytes, d1 flags)
      uint32_t bytes = D(0);
      if (D(1) & 2) bytes = (D(0) & 0xFFFF) * (D(0) >> 16);
      for (uint32_t i = 0; i < bytes; ++i) m.w8(A(1) + i, 0);
      return;
    }
    case 360: {  // InitView(a1)
      for (uint32_t i = 0; i < 18; ++i) m.w8(A(1) + i, 0);
      m.w16(A(1) + 12, FB_VSTART);
      m.w16(A(1) + 14, FB_HSTART);
      return;
    }
    case 384: D(0) = uint32_t(m.vpos_); return;  // VBeamPos
    case 390: {  // InitBitMap(a0 bm, d0 depth, d1 width, d2 height)
      uint32_t bm = A(0);
      m.w16(bm + 0, uint16_t(((D(1) + 15) >> 4) * 2));
      m.w16(bm + 2, uint16_t(D(2)));
      m.w8(bm + 4, 0);
      m.w8(bm + 5, uint8_t(D(0)));
      m.w16(bm + 6, 0);
      return;
    }
    case 402: {  // WaitBOVP(a0 vp)
      uint32_t vp = A(0);
      int bottom = FB_VSTART + int16_t(m.r16(vp + 30)) + int16_t(m.r16(vp + 26));
      if (active_view_) bottom = int16_t(m.r16(active_view_ + 12)) + int16_t(m.r16(vp + 30)) + int16_t(m.r16(vp + 26));
      if (bottom >= LINES_PER_FRAME) bottom = LINES_PER_FRAME - 1;
      m.idle_until([&] { return m.vpos_ >= bottom; });
      return;
    }
    case 408: D(0) = uint32_t(-1); return;  // GetSprite
    case 414: return;                       // FreeSprite
    case 456: case 462: return;             // OwnBlitter / DisownBlitter
    case 492: {  // AllocRaster(d0 w, d1 h)
      D(0) = m.alloc(((D(0) + 15) >> 3 & ~1u) * D(1));
      return;
    }
    case 498: m.free(A(0), ((D(0) + 15) >> 3 & ~1u) * D(1)); return;  // FreeRaster
    case 570: {  // GetColorMap(d0 entries)
      uint32_t n = D(0) & 0xFFFF;
      uint32_t cm = m.alloc(8 + n * 2);
      m.w16(cm + 2, uint16_t(n));
      m.w32(cm + 4, cm + 8);
      D(0) = cm;
      return;
    }
    case 576: return;  // FreeColorMap
    case 540: return;  // FreeVPortCopLists
    case 564: return;  // FreeCprList
    default: unknown(LIB_GFX, lvo); return;
  }
}

// ---- intuition ------------------------------------------------------------------

void Hle::intuition(int lvo) {
  switch (lvo) {
    case 78: D(0) = 1; return;   // CloseWorkBench
    case 210: D(0) = 1; return;  // OpenWorkBench
    case 288: return;            // RemakeDisplay
    case 294: return;            // RethinkDisplay
    default: unknown(LIB_INTUITION, lvo); return;
  }
}

}  // namespace amiga
