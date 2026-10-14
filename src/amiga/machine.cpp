// Machine: memory map, loader, scheduler and 68k<->host call glue.
#include "amiga/machine.hpp"
#include "amiga/hle.hpp"
#include "amiga/hunk.hpp"
#include "native/runtime.hpp"

#include <algorithm>

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
    heap_hw_ = std::max(heap_hw_, a + size);
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
  // Segments are laid out back to back (8-byte LoadSeg header + payload),
  // so WHDLoad PatchSeg offsets map to LOAD_BASE + offset. Same layout as
  // tools/loadseg.py and the recompiler.
  uint32_t a = LOAD_BASE;
  for (auto& h : exe.hunks) {
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
  if (!coverage_path.empty()) {
    executed.assign(end, 0);
    original.assign(ram_.begin(), ram_.begin() + end);
  }
  if (bypass_protection) {
    // Codewheel protection, as patched by JOTD's WHDLoad slave (offsets are
    // relative to the concatenated hunk payloads):
    //   $19737: wrong-answer path stores 1 instead of $FF in the pass flag
    //   $3a964: the pass flag itself, preset to 1
    //   $01a78: NOP the instruction that clears the flag after the check
    write8(patch_address(0x19737), 0x01);
    write8(patch_address(0x3a964), 0x01);
    for (uint32_t i = 0; i < 6; i += 2) write16(patch_address(0x01a78) + i, 0x4E71);
  }
  // CPU-speed independent replacement of the C busy-wait delay() (the
  // WHDLoad slave patches the same loop); keep in sync with tools/recomp.py.
  write16(patch_address(0x008da), uint16_t(0xA000 | (HOST_LIB << 8) | TRAP_DELAY));
  free_list_.clear();
  free_list_[end] = RAM_SIZE - end;
  heap_hw_ = end;
  setup_recomp(end);
  setup_native();
  hle_->boot(hunk_addrs[0], (hunk_addrs[0] - 4) >> 2);
  return true;
}

void Machine::setup_recomp(uint32_t end) {
  recomp_table_.clear();
  const recomp::Image* img = recomp::image();
  if (!use_recomp || !img || !bypass_protection) return;
  // The generated code embeds absolute addresses and the patched code
  // bytes; only use it if the loaded image is identical.
  uint32_t h = 2166136261u;
  for (uint32_t a = img->base; a < img->base + img->size && a < end; ++a)
    h = (h ^ ram_[a]) * 16777619u;
  if (h != img->checksum || img->base != LOAD_BASE) {
    log("recompiled code does not match the loaded executable; interpreting\n");
    return;
  }
  recomp_table_.assign(img->size / 2, nullptr);
  for (size_t i = 0; i < img->count; ++i)
    recomp_table_[(img->entries[i].addr - LOAD_BASE) >> 1] = img->entries[i].fn;
}

// ---- native code -------------------------------------------------------------

void Machine::setup_native() {
  native::g_machine = this;
  calltrace_ = getenv("FA18_CALLTRACE") != nullptr;
  // FA18_WATCH=hexaddr: log every change of the long at addr (needs
  // --interpret so every instruction is visible).
  // FA18_REGS=hexaddr[,count]: log all registers when execution reaches
  // addr (use --native off or the routine's callers will not be seen).
  if (const char* r = getenv("FA18_REGS")) {
    regs_addr_ = uint32_t(strtoul(r, nullptr, 16));
    const char* c = strchr(r, ',');
    regs_log_budget_ = c ? atoi(c + 1) : 20;
  }
  if (const char* w = getenv("FA18_WATCH")) {
    watch_addr_ = uint32_t(strtoul(w, nullptr, 16));
    watch_val_ = read32(watch_addr_);
  }
  native_table_.assign(hunk_addrs.empty() ? 0 : (heap_hw_ - LOAD_BASE) / 2, nullptr);
  // FA18_NATIVE_SKIP=name1,name2 disables individual native routines
  // (FA18_NATIVE_ONLY=... enables only the listed ones); for bisecting.
  native::for_each([](const native::Def& d) {
    amiga::Machine& m = *native::g_machine;
    auto listed = [&](const char* env) {
      const char* v = getenv(env);
      if (!v) return false;
      std::string list = std::string(",") + v + ",";
      return list.find(std::string(",") + d.name + ",") != std::string::npos;
    };
    if (listed("FA18_NATIVE_SKIP")) return;
    if (getenv("FA18_NATIVE_ONLY") && !listed("FA18_NATIVE_ONLY")) return;
    uint32_t idx = (d.addr - LOAD_BASE) >> 1;
    if (idx < m.native_table_.size()) m.native_table_[idx] = &d;
  });
}

void Machine::add_pc_hook(uint32_t addr, std::function<void()> fn) {
  size_t idx = (addr - LOAD_BASE) >> 1;
  if (pc_hooks_.size() <= idx) pc_hooks_.resize(idx + 1, nullptr);
  pc_hooks_[idx] = new std::function<void()>(std::move(fn));
}

void Machine::invoke_native(const native::Def* d) {
  ++native_stats_[d].calls;
  if (d->conv != native::Conv::C) {
    d->fn();
    cpu.pc = cpu.pop32();
    return;
  }
  // Reproduce the original's prologue/epilogue (link a6,#n and movem save
  // of the registers it preserves) so that the 68k stack layout and the
  // register values seen by original callees, and by our caller afterwards,
  // are exactly those of the compiled code. Some original routines depend on
  // them (e.g. reading -n(a6) of their caller, or leftover registers).
  uint32_t pc = d->addr;
  bool link = read16(pc) == 0x4E56;
  int16_t frame = 0;
  if (link) { frame = int16_t(read16(pc + 2)); pc += 4; }
  uint16_t mask = 0;
  if (read16(pc) == 0x48E7) mask = read16(pc + 2);  // movem.l regs,-(sp)
  if (link) {
    cpu.push32(cpu.a[6]);
    cpu.a[6] = cpu.a[7];
    cpu.a[7] += uint32_t(int32_t(frame));
  }
  // -(An) mask: bit 0 = a7 ... bit 15 = d0; store in register order.
  int nsaved = 0;
  for (int i = 0; i < 16; ++i) if (mask & (1 << i)) ++nsaved;
  cpu.a[7] -= uint32_t(nsaved * 4);
  uint32_t save_area = cpu.a[7], p = save_area;
  for (int r = 0; r < 16; ++r) {
    if (!(mask & (1 << (15 - r)))) continue;
    write32(p, r < 8 ? cpu.d[r] : cpu.a[r - 8]);
    p += 4;
  }
  // Arguments are read relative to the entry stack pointer.
  uint32_t entry_sp = link ? cpu.a[6] + 4 : save_area + uint32_t(nsaved * 4);
  native_entry_sp_ = entry_sp;  // c_adapter reads the arguments from here
  d->fn();
  if (watch_addr_ && read32(watch_addr_) != watch_val_) {
    log("[watch] %06x changed during native %s\n", watch_addr_, d->name);
    watch_val_ = read32(watch_addr_);
  }
  // Epilogue: movem.l (sp)+,regs ; unlk a6 ; rts
  p = save_area;
  for (int r = 0; r < 16; ++r) {
    if (!(mask & (1 << (15 - r)))) continue;
    uint32_t v = read32(p);
    if (r < 8) cpu.d[r] = v; else cpu.a[r - 8] = v;
    p += 4;
  }
  if (link) {
    cpu.a[7] = cpu.a[6];
    cpu.a[6] = cpu.pop32();
  } else {
    cpu.a[7] = save_area + uint32_t(nsaved * 4);
  }
  cpu.pc = cpu.pop32();
}

void Machine::call_sub(uint32_t addr) {
  cpu.push32(hle_->ret_stub);
  cpu.pc = addr;
  cpu.stopped = false;
  try {
    for (;;) {
      execute(line_start_ + line_cycles_);
      if (exit_) throw QuitRequested{};
      if (cpu.cycles >= line_start_ + line_cycles_) run_line();
    }
  } catch (ReturnFromCall&) {
  }
}

struct Machine::Snapshot {
  std::vector<uint8_t> ram;
  m68k::Cpu cpu;
  CustomState cs;
};

void Machine::take_snapshot(Snapshot& s) {
  s.ram.assign(ram_.begin(), ram_.begin() + heap_hw_);
  s.cpu = cpu;
  std::memcpy(static_cast<void*>(&s.cs), &cs_, sizeof cs_);
}

void Machine::restore_snapshot(const Snapshot& s) {
  std::copy(s.ram.begin(), s.ram.end(), ram_.begin());
  cpu = s.cpu;
  std::memcpy(static_cast<void*>(&cs_), &s.cs, sizeof cs_);
}

void Machine::verify_native(const native::Def* d) {
  NativeStats& st = native_stats_[d];
  uint32_t entry_sp = cpu.a[7];
  uint32_t ret = read32(entry_sp);
  Snapshot s0, ref, nat;
  take_snapshot(s0);
  suppress_irq_ = true;

  // Reference: the original (recompiled/interpreted) routine.
  write32(entry_sp, hle_->ret_stub);
  in_reference_ = true;
  bool returned = false;
  try {
    execute(cpu.cycles + 200'000'000);
  } catch (ReturnFromCall&) {
    returned = true;
  }
  in_reference_ = false;
  if (!returned || exit_) {
    // Too long or blocking; just run the native version.
    restore_snapshot(s0);
    suppress_irq_ = false;
    ++st.skipped;
    invoke_native(d);
    return;
  }
  cpu.pc = ret;
  write32(entry_sp, ret);
  take_snapshot(ref);

  // Native version from the same state.
  restore_snapshot(s0);
  invoke_native(d);
  take_snapshot(nat);
  suppress_irq_ = false;
  ++st.verified;

  // Compare. Stack memory below the entry SP is scratch.
  std::string diff;
  int ndiff = 0;
  auto note = [&](const char* fmt, auto... a) {
    if (ndiff++ < 12) {
      char b[160];
      snprintf(b, sizeof b, fmt, a...);
      diff += b;
    }
  };
  uint32_t stack_lo = entry_sp >= 0x10000 && entry_sp < 0x20000 ? 0x10000 : entry_sp;
  // C callees may modify their own argument slots (the caller pops them).
  uint32_t args_lo = entry_sp + 4, args_hi = d->conv == native::Conv::C ? args_lo + 4u * uint32_t(d->nargs) : args_lo;
  for (uint32_t a = 0; a < heap_hw_; ++a) {
    if (a >= stack_lo && a < entry_sp) continue;
    if (a >= args_lo && a < args_hi) continue;
    if (ref.ram[a] != nat.ram[a]) note(" [%06x] ref=%02x nat=%02x", a, ref.ram[a], nat.ram[a]);
  }
  bool c_conv = d->conv == native::Conv::C;
  for (int i = 0; i < 8; ++i) {
    if (c_conv && (i == 1 || (i == 0 && !d->ret_d0))) continue;
    if (ref.cpu.d[i] != nat.cpu.d[i]) note(" d%d ref=%08x nat=%08x", i, ref.cpu.d[i], nat.cpu.d[i]);
  }
  for (int i = c_conv ? 2 : 0; i < 8; ++i)
    if (ref.cpu.a[i] != nat.cpu.a[i]) note(" a%d ref=%08x nat=%08x", i, ref.cpu.a[i], nat.cpu.a[i]);
  if (ref.cpu.pc != nat.cpu.pc) note(" pc ref=%06x nat=%06x", ref.cpu.pc, nat.cpu.pc);
  if (std::memcmp(&ref.cs, &nat.cs, sizeof ref.cs) != 0) note(" custom chip state differs%s", "");
  if (ndiff) {
    ++st.mismatches;
    if (st.mismatches <= 5)
      log("native verify MISMATCH %s (%06x), %d differences:%s\n", d->name, d->addr, ndiff, diff.c_str());
    // Continue with the original's results.
    restore_snapshot(ref);
  }
}

void Machine::print_native_stats() {
  for (auto& [d, st] : native_stats_)
    log("native %-28s %06x calls=%llu verified=%llu mismatches=%llu skipped=%llu\n", d->name,
        d->addr, (unsigned long long)st.calls, (unsigned long long)st.verified,
        (unsigned long long)st.mismatches, (unsigned long long)st.skipped);
}

uint32_t Machine::patch_address(uint32_t offset) const {
  for (size_t i = 0; i < hunk_addrs.size(); ++i) {
    if (offset < hunk_sizes[i]) return hunk_addrs[i] + offset;
    offset -= hunk_sizes[i];
  }
  return 0;
}

void Machine::write_coverage() {
  if (coverage_path.empty() || executed.empty()) return;
  FILE* f = fopen(coverage_path.c_str(), "w");
  if (!f) return;
  for (uint32_t a = 0; a < executed.size(); ++a)
    if (executed[a]) fprintf(f, "x %06x\n", a);
  // Changed bytes inside hunks (self-modifying code / data in code hunks)
  for (size_t h = 0; h < hunk_addrs.size(); ++h) {
    for (uint32_t a = hunk_addrs[h]; a < hunk_addrs[h] + hunk_sizes[h]; ++a)
      if (ram_[a] != original[a]) fprintf(f, "w %06x %zu\n", a, h);
  }
  fclose(f);
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
      case TRAP_DELAY: {
        // delay(n): C busy loop "for (i = 0; i < n; i++);" (~76 clocks per
        // iteration on a 7 MHz 68000) followed by FreeSprite(0). Wait the
        // same amount of A500 time regardless of the CPU speed setting.
        uint32_t n = read32(cpu.a[7] + 4);
        idle_lines(int(uint64_t(n) * 76 / CYCLES_PER_LINE));
        cpu.pc = cpu.pop32();
        return true;
      }
    }
    return false;
  }
  hle(lib, idx * 6);
  return true;
}

void Machine::hle(int lib, int lvo) { hle_->call(lib, lvo); }

uint32_t Machine::call68k(uint32_t addr, const uint32_t* d, const uint32_t* a) {
  m68k::Cpu::Regs saved = cpu.save();
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
      execute(line_start_ + line_cycles_);
      if (cpu.stopped) { log("call68k: cpu stopped in callback %06x\n", addr); break; }
      if (exit_) break;
      if (cpu.cycles >= line_start_ + line_cycles_) run_line();
    }
  } catch (ReturnFromCall&) {
  }
  --call_depth_;
  uint32_t ret = cpu.d[0];
  last_call_z_ = cpu.fz;
  cpu.restore(saved);
  return ret;
}

// ---- scheduler -------------------------------------------------------------

void Machine::run_line() {
  // Called when the CPU has consumed a scanline worth of cycles.
  line_start_ += line_cycles_;
  ++total_lines_;
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
    if (det_timing_) {
      line_start_ = cpu.cycles - line_cycles_;  // run_line() adds line_cycles_
    } else {
      cpu.cycles = line_start_ + line_cycles_;
    }
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
  if (ram_dump_frame && frame_count_ == ram_dump_frame) {
    if (FILE* f = fopen(ram_dump_path.c_str(), "wb")) {
      fwrite(ram_.data(), 1, heap_hw_, f);
      fclose(f);
    }
  }
  if (ram_hash_every > 0 && frame_count_ % uint64_t(ram_hash_every) == 0) {
    // Game memory above the stacks (program image and heap).
    uint64_t h = 1469598103934665603ull;
    for (uint32_t a = LOAD_BASE; a < heap_hw_; ++a) h = (h ^ ram_[a]) * 1099511628211ull;
    log("[ramhash] frame %llu %016llx\n", (unsigned long long)frame_count_, (unsigned long long)h);
  }
  if (verbose && frame_count_ % 60 == 0) {
    static uint64_t last_lv = 0;
    log("[frame %llu] pc=%06x game fps=%llu\n", (unsigned long long)frame_count_, cpu.pc,
        (unsigned long long)(loadview_count - last_lv));
    last_lv = loadview_count;
  }
  if (host_) {
    if (hires_scale > 0 && remaster::compose(*this, hires_fb_))
      host_->frame_hires(hires_fb_.data(), FB_W * hires_scale, FB_H * hires_scale);
    else
      host_->frame(fb_.data());
    host_->audio(audio_buf_.data(), audio_buf_.size() / 2);
  }
  audio_buf_.clear();
  hle_->frame_tick();
  if (!pending_load_.empty() && !in_main_loop) throw LoadRequested{};
}

void Machine::set_hires_scale(int s) {
  static bool installed = false;
  if (s > 0 && !installed) {
    remaster::install(*this);
    installed = true;
  }
  hires_scale = s < 0 ? 0 : (s > 8 ? 8 : s);
}

void Machine::apply_state_requests() {
  if (!pending_save_.empty()) {
    std::string p;
    p.swap(pending_save_);
    save_state_now(p);
    log("[state] %s\n", state_msg_.c_str());
  }
  if (!pending_load_.empty()) {
    std::string p;
    p.swap(pending_load_);
    load_state_now(p);
    log("[state] %s\n", state_msg_.c_str());
  }
}

void Machine::check_interrupts() {
  if (suppress_irq_) return;
  if (!(cs_.intena_ & INT_INTEN)) return;
  uint16_t pending = cs_.intena_ & cs_.intreq_ & 0x3FFF;
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

void Machine::execute(int64_t until) {
  while (cpu.cycles < until && !exit_) {
    if (cpu.stopped) { cpu.cycles = until; return; }
    uint32_t pc = cpu.pc;
    if (calltrace_) {
      static uint64_t from = 0, to = 0;
      static FILE* f = nullptr;
      if (!f) {
        const char* v = getenv("FA18_CALLTRACE");  // from,to,file
        char path[512] = {};
        sscanf(v, "%lu,%lu,%511s", &from, &to, path);
        f = fopen(path, "w");
        if (!f) calltrace_ = false;
      }
      if (f && frame_count_ >= from && frame_count_ <= to) fprintf(f, "%llu %06x %08x %08x %08x\n", (unsigned long long)frame_count_, pc, read32(cpu.a[7] + 4), read32(cpu.a[7] + 8), read32(cpu.a[7] + 12));
    }
    if (regs_addr_ && pc == regs_addr_ && regs_log_budget_ > 0) {
      --regs_log_budget_;
      log("[regs] pc=%06x frame %llu d:", pc, (unsigned long long)frame_count_);
      for (int i = 0; i < 8; ++i) log(" %08x", cpu.d[i]);
      log(" a:");
      for (int i = 0; i < 8; ++i) log(" %08x", cpu.a[i]);
      log("\n");
    }
    if (!pc_hooks_.empty() && !in_reference_) {
      uint32_t hidx = (pc - LOAD_BASE) >> 1;
      if (hidx < pc_hooks_.size() && pc_hooks_[hidx] && !(pc & 1)) (*pc_hooks_[hidx])();
    }
    if (native_mode != NativeMode::Off && !in_reference_) {
      uint32_t idx = (pc - LOAD_BASE) >> 1;
      if (idx < native_table_.size() && native_table_[idx] && !(pc & 1)) {
        const native::Def* d = native_table_[idx];
        // Verify the first 200 calls of each routine, then every 50th
        // (FA18_VERIFY_ALL=1: every call).
        static const bool verify_all = getenv("FA18_VERIFY_ALL") != nullptr;
        NativeStats& st = native_stats_[d];
        if (native_mode == NativeMode::Verify && d->verify &&
            (verify_all || st.calls < 200 || st.calls % 50 == 0))
          verify_native(d);
        else invoke_native(d);
        continue;
      }
    }
    if (!recomp_table_.empty()) {
      uint32_t idx = (pc - LOAD_BASE) >> 1;
      if (idx < recomp_table_.size() && recomp_table_[idx] && !(pc & 1)) {
        recomp_table_[idx](cpu, until);
        continue;
      }
      if (recomp_misses_ && pc >= LOAD_BASE && pc < LOAD_BASE + recomp_table_.size() * 2)
        recomp_misses_->insert(pc);
    }
    if (!executed.empty() && pc < executed.size()) executed[pc] = 1;
    if (trace) log("%06x  %04x  d0=%08x d1=%08x a0=%08x a7=%08x\n", pc, read16(pc), cpu.d[0], cpu.d[1], cpu.a[0], cpu.a[7]);
    cpu.step();
    if (watch_addr_) {
      uint32_t v = read32(watch_addr_);
      if (v != watch_val_) {
        log("[watch] %06x: %08x -> %08x at pc=%06x (op_pc=%06x) frame %llu a7=%06x a6=%06x stack:",
            watch_addr_, watch_val_, v, cpu.pc, cpu.op_pc, (unsigned long long)frame_count_,
            cpu.a[7], cpu.a[6]);
        for (int i = 0; i < 6; ++i) log(" %06x", read32(cpu.a[7] + uint32_t(i) * 4));
        log("\n");
        watch_val_ = v;
      }
    }
  }
}

int Machine::run() {
  for (;;) {
    try {
      if (resume_main_loop_) {
        // a state was loaded while the C++ stack was in the intro: continue
        // in the game's main loop, which holds no state of its own
        resume_main_loop_ = false;
        native::resume_main_loop();
      }
      while (!exit_) {
        execute(line_start_ + line_cycles_);
        if (exit_) break;
        if (cpu.cycles >= line_start_ + line_cycles_) run_line();
      }
      break;
    } catch (QuitRequested&) {
      break;
    } catch (LoadRequested&) {
      std::string p;
      p.swap(pending_load_);
      load_state_now(p);
      log("[state] %s\n", state_msg_.c_str());
      resume_main_loop_ = true;
    }
  }
  write_coverage();
  return exit_code_;
}

}  // namespace amiga
