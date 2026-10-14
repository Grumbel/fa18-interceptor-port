// Save states: RAM, CPU, custom chips, time, input and the HLE (OS) state.
// Native C++ routines keep all their state in emulated RAM, so this is the
// complete machine.  Applied between scanline chunks of Machine::run().
#include "amiga/hle.hpp"
#include "amiga/machine.hpp"
#include "amiga/ser.hpp"

#include <cstdio>

namespace amiga {

namespace {
constexpr uint32_t MAGIC = 0x46413138;  // "FA18"
constexpr uint32_t VERSION = 1;
}  // namespace

void Hle::ser(Ser& s) {
  s.pod(ret_stub);
  s.pod(exit_stub);
  s.pod(low_next_);
  s.bytes(libs_, sizeof libs_);
  s.pod(task_);
  s.pod(input_task_);
  s.pod(cli_);
  s.pod(sig_alloc_);
  s.pod(sig_recvd_);
  s.pod(disable_);
  s.pod(forbid_);
  s.pod(cur_level_);
  for (auto& v : servers_) s.vec(v);
  s.bytes(vectors_, sizeof vectors_);
  s.dq(kbd_reads_);
  s.dq(gameport_reads_);
  s.dq(kbd_queue_);
  s.dq(fire_queue_);
  s.vec(timers_);
  s.bytes(key_matrix_, sizeof key_matrix_);
  s.pod(qualifier_);
  s.pod(gameport_type_);
  s.vec(input_handlers_);
  s.pod(active_view_);
  s.set_pod(cprlists_);
  {
    uint32_t c = s.count(color_moves_.size());
    if (s.loading) {
      color_moves_.clear();
      for (uint32_t i = 0; i < c; ++i) {
        uint32_t k = 0;
        s.pod(k);
        s.vec(color_moves_[k]);
      }
    } else {
      for (auto& [k, v] : color_moves_) {
        uint32_t kk = k;
        s.pod(kk);
        s.vec(v);
      }
    }
  }
  {  // open files: reopened by path at the saved position
    uint32_t c = s.count(files_.size());
    if (s.loading) {
      for (auto& [h, f] : files_) if (f.f) std::fclose(f.f);
      files_.clear();
      for (uint32_t i = 0; i < c; ++i) {
        uint32_t h = 0;
        int64_t pos = 0;
        File f;
        s.pod(h);
        s.str(f.path);
        s.str(f.name);
        s.pod(f.writable);
        s.pod(pos);
        f.f = std::fopen(f.path.c_str(), f.writable ? "r+b" : "rb");
        if (f.f) std::fseek(f.f, long(pos), SEEK_SET);
        files_[h] = f;
      }
    } else {
      for (auto& [h, f] : files_) {
        uint32_t hh = h;
        int64_t pos = f.f ? std::ftell(f.f) : 0;
        File ff = f;
        s.pod(hh);
        s.str(ff.path);
        s.str(ff.name);
        s.pod(ff.writable);
        s.pod(pos);
      }
    }
  }
  {
    uint32_t c = s.count(locks_.size());
    if (s.loading) {
      locks_.clear();
      for (uint32_t i = 0; i < c; ++i) {
        uint32_t k = 0;
        std::string v;
        s.pod(k);
        s.str(v);
        locks_[k] = v;
      }
    } else {
      for (auto& [k, v] : locks_) {
        uint32_t kk = k;
        std::string vv = v;
        s.pod(kk);
        s.str(vv);
      }
    }
  }
  s.pod(next_handle_);
  s.pod(ioerr_);
}

bool Machine::save_state_now(const std::string& path) {
  Ser s;
  uint32_t magic = MAGIC, ver = VERSION;
  s.pod(magic);
  s.pod(ver);
  uint32_t hw = heap_hw_;
  s.pod(hw);
  s.bytes(ram_.data(), hw);
  s.pod(cpu.d);
  s.pod(cpu.a);
  s.pod(cpu.osp);
  s.pod(cpu.pc);
  s.pod(cpu.s);
  s.pod(cpu.t);
  s.pod(cpu.imask);
  s.pod(cpu.fx);
  s.pod(cpu.fn);
  s.pod(cpu.fz);
  s.pod(cpu.fv);
  s.pod(cpu.fc);
  s.pod(cpu.stopped);
  s.pod(cpu.cycles);
  s.pod(cpu.op_pc);
  s.pod(cs_);
  s.pod(line_start_);
  s.pod(total_lines_);
  s.pod(vpos_);
  s.pod(frame_count_);
  s.pod(call_depth_);
  s.pod(last_call_z_);
  s.pod(audio_frac_);
  s.pod(lp_l_);
  s.pod(lp_r_);
  s.pod(mouse_x_);
  s.pod(mouse_y_);
  s.pod(lmb_);
  s.pod(rmb_);
  s.pod(joy_up_);
  s.pod(joy_down_);
  s.pod(joy_left_);
  s.pod(joy_right_);
  s.pod(joy_fire_);
  s.map_pod(free_list_);
  hle_->ser(s);
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) { state_msg_ = "save failed: cannot write " + path; return false; }
  std::fwrite(s.buf.data(), 1, s.buf.size(), f);
  std::fclose(f);
  state_msg_ = "state saved";
  return true;
}

bool Machine::load_state_now(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) { state_msg_ = "no saved state"; return false; }
  Ser s;
  s.loading = true;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  s.buf.resize(size_t(n));
  size_t got = std::fread(s.buf.data(), 1, size_t(n), f);
  std::fclose(f);
  if (got != size_t(n)) { state_msg_ = "load failed: short read"; return false; }
  uint32_t magic = 0, ver = 0, hw = 0;
  s.pod(magic);
  s.pod(ver);
  s.pod(hw);
  if (magic != MAGIC || ver != VERSION || hw > ram_.size()) {
    state_msg_ = "load failed: incompatible state file";
    return false;
  }
  heap_hw_ = hw;
  s.bytes(ram_.data(), hw);
  s.pod(cpu.d);
  s.pod(cpu.a);
  s.pod(cpu.osp);
  s.pod(cpu.pc);
  s.pod(cpu.s);
  s.pod(cpu.t);
  s.pod(cpu.imask);
  s.pod(cpu.fx);
  s.pod(cpu.fn);
  s.pod(cpu.fz);
  s.pod(cpu.fv);
  s.pod(cpu.fc);
  s.pod(cpu.stopped);
  s.pod(cpu.cycles);
  s.pod(cpu.op_pc);
  s.pod(cs_);
  s.pod(line_start_);
  s.pod(total_lines_);
  s.pod(vpos_);
  s.pod(frame_count_);
  s.pod(call_depth_);
  s.pod(last_call_z_);
  s.pod(audio_frac_);
  s.pod(lp_l_);
  s.pod(lp_r_);
  s.pod(mouse_x_);
  s.pod(mouse_y_);
  s.pod(lmb_);
  s.pod(rmb_);
  s.pod(joy_up_);
  s.pod(joy_down_);
  s.pod(joy_left_);
  s.pod(joy_right_);
  s.pod(joy_fire_);
  s.map_pod(free_list_);
  hle_->ser(s);
  audio_buf_.clear();
  remaster::reset();
  state_msg_ = s.ok ? "state loaded" : "state loaded (file damaged?)";
  return s.ok;
}

}  // namespace amiga
