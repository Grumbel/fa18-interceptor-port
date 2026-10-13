// Runs the SingleStepTests 68000 suite (https://github.com/SingleStepTests/m68000)
// against the interpreter.  Usage: cpu_sst <file.json.bin>...
// Tests that involve address errors are skipped (not emulated).
#include "m68k/cpu.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct RamBus : m68k::Bus {
  std::vector<uint8_t> mem;
  RamBus() : mem(1 << 24) { ram = mem.data(); ram_size = 1 << 24; }
  uint8_t io_read8(uint32_t) override { return 0; }
  uint16_t io_read16(uint32_t) override { return 0; }
  void io_write8(uint32_t, uint8_t) override {}
  void io_write16(uint32_t, uint16_t) override {}
};

struct State {
  uint32_t r[19];  // d0-7 a0-6 usp ssp sr pc
  uint32_t pf[2];
  std::vector<std::pair<uint32_t, uint16_t>> ram;
};

struct Reader {
  const uint8_t* p;
  uint32_t u32() { uint32_t v; std::memcpy(&v, p, 4); p += 4; return v; }
  uint16_t u16() { uint16_t v; std::memcpy(&v, p, 2); p += 2; return v; }
  uint8_t u8() { return *p++; }
};

State read_state(Reader& rd) {
  State s;
  rd.u32(); rd.u32();
  for (auto& v : s.r) v = rd.u32();
  s.pf[0] = rd.u32(); s.pf[1] = rd.u32();
  uint32_t n = rd.u32();
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t a = rd.u32();
    uint16_t d = rd.u16();
    s.ram.push_back({a, d});
  }
  return s;
}

const char* RN[19] = {"d0","d1","d2","d3","d4","d5","d6","d7","a0","a1","a2","a3","a4","a5","a6","usp","ssp","sr","pc"};

}  // namespace

int main(int argc, char** argv) {
  RamBus bus;
  m68k::Cpu cpu;
  cpu.bus = &bus;
  int total_fail = 0, total = 0, total_skip = 0;
  bool verbose = getenv("SST_VERBOSE") != nullptr;
  for (int f = 1; f < argc; ++f) {
    std::ifstream in(argv[f], std::ios::binary);
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)), {});
    Reader rd{buf.data()};
    rd.u32();
    uint32_t ntests = rd.u32();
    int fails = 0, skips = 0, shown = 0;
    for (uint32_t t = 0; t < ntests; ++t) {
      rd.u32(); rd.u32();
      rd.u32(); rd.u32();
      uint32_t slen = rd.u32();
      std::string name(reinterpret_cast<const char*>(rd.p), slen);
      rd.p += slen;
      State ini = read_state(rd);
      State fin = read_state(rd);
      rd.u32(); rd.u32();
      rd.u32();
      uint32_t ntr = rd.u32();
      bool addr_err = false;
      for (uint32_t i = 0; i < ntr; ++i) {
        uint8_t tw = rd.u8();
        rd.u32();
        if (tw == 0) continue;
        if (tw == 4 || tw == 5) addr_err = true;
        rd.p += 20;
      }
      if (addr_err) { ++skips; continue; }

      for (auto& [a, d] : ini.ram) { bus.mem[a] = uint8_t(d >> 8); bus.mem[a | 1] = uint8_t(d); }
      uint32_t pc = ini.r[18] - 4;
      bus.mem[pc] = uint8_t(ini.pf[0] >> 8); bus.mem[pc + 1] = uint8_t(ini.pf[0]);
      bus.mem[pc + 2] = uint8_t(ini.pf[1] >> 8); bus.mem[pc + 3] = uint8_t(ini.pf[1]);
      for (int i = 0; i < 8; ++i) cpu.d[i] = ini.r[i];
      for (int i = 0; i < 7; ++i) cpu.a[i] = ini.r[8 + i];
      cpu.s = false;
      cpu.a[7] = ini.r[15];
      cpu.osp = ini.r[16];
      cpu.set_sr(uint16_t(ini.r[17]));
      cpu.pc = pc;
      cpu.stopped = false;
      cpu.step();

      uint32_t got[19];
      for (int i = 0; i < 8; ++i) got[i] = cpu.d[i];
      for (int i = 0; i < 7; ++i) got[8 + i] = cpu.a[i];
      got[15] = cpu.s ? cpu.osp : cpu.a[7];
      got[16] = cpu.s ? cpu.a[7] : cpu.osp;
      got[17] = cpu.sr();
      got[18] = cpu.pc + 4;
      bool ok = true;
      std::string why;
      for (int i = 0; i < 19; ++i) {
        uint32_t want = fin.r[i];
        if (i == 17) { want &= 0xA71F; got[i] &= 0xA71F; }
        if (got[i] != want) {
          ok = false;
          char b[96];
          snprintf(b, sizeof b, " %s=%08x(want %08x)", RN[i], got[i], want);
          why += b;
        }
      }
      for (auto& [a, d] : fin.ram) {
        uint16_t g = uint16_t(bus.mem[a] << 8 | bus.mem[a | 1]);
        if (g != d) {
          ok = false;
          char b[96];
          snprintf(b, sizeof b, " [%06x]=%04x(want %04x)", a, g, d);
          why += b;
        }
      }
      // clear touched memory
      for (auto& [a, d] : ini.ram) { bus.mem[a] = 0; bus.mem[a | 1] = 0; }
      for (auto& [a, d] : fin.ram) { bus.mem[a] = 0; bus.mem[a | 1] = 0; }
      for (int i = 0; i < 4; ++i) bus.mem[(pc + i) & 0xFFFFFF] = 0;
      if (!ok) {
        ++fails;
        if (shown < 3 || verbose) {
          ++shown;
          printf("  FAIL %s:%s\n", name.c_str(), why.c_str());
        }
      }
    }
    printf("%-28s %5u tests %5d fail %5d skipped\n", argv[f], ntests, fails, skips);
    total_fail += fails;
    total += int(ntests);
    total_skip += skips;
  }
  printf("TOTAL %d tests, %d failed, %d skipped\n", total, total_fail, total_skip);
  return total_fail ? 1 : 0;
}
