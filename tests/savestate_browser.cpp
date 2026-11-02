#include "savestate_browser.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

void write_sample(const fs::path& path, int mission) {
  constexpr uint32_t heap = 0x5d000;
  uint32_t hdr[] = {0x46413138u, 1u, heap};
  std::vector<uint8_t> ram(heap);
  auto byte = [&](uint32_t addr, int value) { ram[addr] = uint8_t(value); };
  auto word = [&](uint32_t addr, int value) {
    byte(addr, value >> 8);
    byte(addr + 1, value);
  };
  auto longword = [&](uint32_t addr, int value) {
    word(addr, value >> 16);
    word(addr + 2, value);
  };
  byte(0x5aa8d, 1);
  byte(0x5ab9e, mission);
  byte(0x5aba2, 4);
  byte(0x5aba3, 1);
  word(0x5abd6, 0);
  word(0x5b47c + 6, 12);
  word(0x5b47c + 8, 34);
  longword(0x5b47c + 24, 250 * 1024);
  word(0x5b47c + 110, 360 * 12);
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(hdr), sizeof hdr);
  f.write(reinterpret_cast<const char*>(ram.data()), ram.size());
}

int main(int argc, char** argv) {
  using namespace savestate_browser;
  if (argc > 1) {  // Optional real-save diagnostic.
    std::string label;
    if (!describe(argv[1], label)) return 1;
    std::cout << label << '\n';
    return 0;
  }
  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto dir = fs::temp_directory_path() / ("fa18-savestate-browser-test-" + std::to_string(unique));
  fs::create_directories(dir);
  for (int i = 0; i < 25; ++i)
    write_sample(dir / ("state" + std::to_string(i + 1) + ".sav"), 5);
  std::ofstream(dir / "invalid.sav") << "not a state";
  std::ofstream(dir / "ignored.txt") << "text";

  auto states = list(dir);
  assert(states.size() == 25);  // No four-slot limit, includes legacy names.
  assert(states[0].label == "M5 INTERCEPT C12,34 1250FT 360KT 1/4");
  std::string desc;
  assert(!describe(dir / "invalid.sav", desc));
  assert(!describe(dir / "missing.sav", desc));
  auto first = new_path(dir);
  assert(first.extension() == ".sav");
  write_sample(first, 7);
  auto second = new_path(dir);
  assert(first != second);  // Collision-proof new-save naming.
  assert(list(dir).size() == 26);

  fs::remove_all(dir);
}
