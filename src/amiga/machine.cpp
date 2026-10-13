// Machine: memory map, loader, scheduler and 68k<->host call glue.
#include "amiga/machine.hpp"
#include "amiga/hle.hpp"
#include "amiga/hunk.hpp"

#include <cstdarg>
#include <cstring>

namespace amiga {

Machine::Machine() : ram_(RAM_SIZE), fb_(FB_W * FB_H) {
  ram = ram_.data();
  ram_size = RAM_SIZE;
  cpu.bus = this;
  cpu.line_a = [this](m68k::Cpu&, uint16_t op) { return line_a(op); };
  cpu.fatal = [this](m68k::Cpu& c, int vec) {
    log("FATAL: exception vector %d at pc=%06x (op_pc=%06x)\n", vec, c.pc, c.op_pc);
    for (int i = 0; i < 8; ++i) log("  d%d=%08x a%d=%08x\n", i, c.d[i], i, c.a[i]);
    exit_ = true;
    exit_code_ = 100;
  };
  hle_ = std::make_unique<Hle>(*this);
}

Machine::~Machine() = default;

void Machine::log(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
}

std::string Machine::cstr(uint32_t a, size_t max) {
  std::string s;
  for (size_t i = 0; i < max; ++i) {
    char ch = char(read8(a + uint32_t(i)));
    if (!ch) break;
    s += ch;
  }
  return s;
}

std::string Machine::bstr(uint32_t bptr) {
  uint32_t a = bptr << 2;
  uint8_t n = read8(a);
  std::string s;
  for (int i = 0; i < n; ++i) s += char(read8(a + 1 + uint32_t(i)));
  return s;
}

// ---- heap ------------------------------------------------------------------

uint32_t Machine::alloc(uint32_t size, bool clear) {
  size = (size + 7) & ~7u;
  if (size == 0) size = 8;
  for (auto it = free_list_.begin(); it != free_list_.end(); ++it) {
    if (it->second < size) continue;
    uint32_t a = it->first;
    uint32_t rest = it->second - size;
    free_list_.erase(it);
    if (rest) free_list_[a + size] = rest;
    if (clear) std::memset(&ram_[a], 0, size);
    return a;
  }
  return 0;
}

void Machine::free(uint32_t addr, uint32_t size) {
  if (!addr) return;
  size = (size + 7) & ~7u;
  if (size == 0) size = 8;
  auto [it, ok] = free_list_.emplace(addr, size);
  if (!ok) { log("double free %06x\n", addr); return; }
  // merge with next
  auto next = std::next(it);
  if (next != free_list_.end() && it->first + it->second == next->first) {
    it->second += next->second;
    free_list_.erase(next);
  }
  // merge with previous
  if (it != free_list_.begin()) {
    auto prev = std::prev(it);
    if (prev->first + prev->second == it->first) {
      prev->second += it->second;
      free_list_.erase(it);
    }
  }
}

uint32_t Machine::avail() const {
  uint32_t total = 0;
  for (auto& [a, s] : free_list_) total += s;
  return total;
}

// ---- boot ------------------------------------------------------------------

bool Machine::boot(const std::string& dir) {
  data_dir = dir;
  if (save_dir.empty()) save_dir = dir;
  HunkFile exe;
  std::string path = dir + "/F-18 Interceptor";
  if (!exe.load(path)) {
    log("cannot load %s\n", path.c_str());
    return false;
  }
  // Same layout as tools/loadseg.py and the recompiler.
  uint32_t a = LOAD_BASE;
  for (auto& h : exe.hunks) {
    a = (a + 7) & ~7u;
    hunk_addrs.push_back(a + 8);
    hunk_sizes.push_back(h.alloc_size);
    a += 8 + h.alloc_size;
  }
  uint32_t end = (a + 0xFFF) & ~0xFFFu;
  for (size_t i = 0; i < exe.hunks.size(); ++i) {
    auto& h = exe.hunks[i];
    uint32_t base = hunk_addrs[i];
    std::memcpy(&ram_[base], h.data.data(), h.data.size());
    write32(base - 8, h.alloc_size + 8);
    write32(base - 4, i + 1 < exe.hunks.size() ? (hunk_addrs[i + 1] - 4) >> 2 : 0);
    for (auto& [target, offs] : h.relocs)
      for (uint32_t o : offs) write32(base + o, read32(base + o) + hunk_addrs[target]);
  }
  free_list_.clear();
  free_list_[end] = RAM_SIZE - end;
  hle_->boot(hunk_addrs[0], (hunk_addrs[0] - 4) >> 2);
  return true;
}

// ---- 68k <-> host calls ----------------------------------------------------

bool Machine::line_a(uint16_t op) {
  int lib = (op >> 8) & 0xF;
  int idx = op & 0xFF;
  if (lib == HOST_LIB) {
    switch (idx) {
      case TRAP_RETURN:
        throw ReturnFromCall{};
      case TRAP_EXIT:
        exit_ = true;
        exit_code_ = int(cpu.d[0]);
        cpu.stopped = true;
        return true;
    }
    return false;
  }
  hle(lib, idx * 6);
  return true;
}

void Machine::hle(int lib, int lvo) { hle_->call(lib, lvo); }

uint32_t Machine::call68k(uint32_t addr, const uint32_t* d, const uint32_t* a) {
  m68k::Cpu saved = cpu;
  if (d) for (int i = 0; i < 8; ++i) cpu.d[i] = d[i];
  if (a) for (int i = 0; i < 7; ++i) cpu.a[i] = a[i];
  // Leave some room below the interrupted code's stack.
  cpu.a[7] = (saved.a[7] - 64) & ~1u;
  cpu.push32(hle_->ret_stub);
  cpu.pc = addr;
  cpu.stopped = false;
  ++call_depth_;
  try {
    for (;;) {
      int64_t until = line_start_ + CYCLES_PER_LINE;
      while (cpu.cycles < until) {
        cpu.step();
        if (cpu.stopped) break;
      }
      if (cpu.stopped) { log("call68k: cpu stopped in callback %06x\n", addr); break; }
      if (cpu.cycles >= line_start_ + CYCLES_PER_LINE) run_line();
    }
  } catch (ReturnFromCall&) {
  }
  --call_depth_;
  uint32_t ret = cpu.d[0];
  last_call_z_ = cpu.fz;
  int64_t cyc = cpu.cycles;
  cpu = saved;
  cpu.cycles = cyc;
  return ret;
}

// ---- scheduler -------------------------------------------------------------

void Machine::run_line() {
  // Called when the CPU has consumed a scanline worth of cycles.
  line_start_ += CYCLES_PER_LINE;
  audio_line();
  ++vpos_;
  if (vpos_ >= LINES_PER_FRAME) {
    vpos_ = 0;
    end_of_frame();
    raise(INT_VERTB);
  }
  hle_->line_tick(vpos_);
  check_interrupts();
}

void Machine::idle_lines(int n) {
  for (int i = 0; i < n; ++i) {
    cpu.cycles = line_start_ + CYCLES_PER_LINE;
    run_line();
  }
}

void Machine::idle_until(const std::function<bool()>& done) {
  while (!done()) {
    if (exit_) throw QuitRequested{};
    idle_lines(1);
  }
}

void Machine::end_of_frame() {
  render_frame();
  ++frame_count_;
  if (verbose && frame_count_ % 50 == 0)
    log("[frame %llu] pc=%06x sr=%04x a7=%06x\n", (unsigned long long)frame_count_, cpu.pc, cpu.sr(), cpu.a[7]);
  if (host_) {
    host_->frame(fb_.data());
    host_->audio(audio_buf_.data(), audio_buf_.size() / 2);
  }
  audio_buf_.clear();
  hle_->frame_tick();
}

void Machine::check_interrupts() {
  if (!(intena_ & INT_INTEN)) return;
  uint16_t pending = intena_ & intreq_ & 0x3FFF;
  if (!pending) return;
  if (hle_->interrupts_disabled()) return;
  // Highest level first.
  if (pending & (INT_EXTER)) dispatch_interrupt(6);
  if (pending & (INT_AUD0 | INT_AUD1 | INT_AUD2 | INT_AUD3)) dispatch_interrupt(4);
  if (pending & (INT_COPER | INT_VERTB | INT_BLIT)) dispatch_interrupt(3);
  if (pending & (INT_PORTS)) dispatch_interrupt(2);
  if (pending & (INT_TBE | INT_DSKBLK | INT_SOFT)) dispatch_interrupt(1);
}

void Machine::dispatch_interrupt(int level) { hle_->interrupt(level); }

int Machine::run() {
  try {
    while (!exit_) {
      int64_t until = line_start_ + CYCLES_PER_LINE;
      while (cpu.cycles < until && !exit_) {
        if (cpu.stopped) { cpu.cycles = until; break; }
        if (trace) log("%06x  %04x  d0=%08x d1=%08x a0=%08x a7=%08x\n", cpu.pc, read16(cpu.pc), cpu.d[0], cpu.d[1], cpu.a[0], cpu.a[7]);
        cpu.step();
      }
      if (exit_) break;
      run_line();
    }
  } catch (QuitRequested&) {
    return 0;
  }
  return exit_code_;
}

}  // namespace amiga
