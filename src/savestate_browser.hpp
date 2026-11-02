// Read-only save-state index for the host menu.  Descriptions come from the
// Amiga RAM snapshot inside each state, not from its filename.  No game data
// needs to be loaded into the live Machine while browsing.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace savestate_browser {

namespace fs = std::filesystem;

struct Entry {
  fs::path path;
  std::string label;
  fs::file_time_type modified;
};

inline bool read_exact(std::ifstream& f, uint64_t offset, void* dst, size_t n) {
  f.seekg(std::streamoff(offset), std::ios::beg);
  return bool(f.read(static_cast<char*>(dst), std::streamsize(n)));
}

inline const char* mission_name(int n) {
  switch (n) {
    case 1: return "FREE";
    case 2: return "CARRIER";
    case 3: return "IDENT";
    case 4: return "DEFENSE";
    case 5: return "INTERCEPT";
    case 6: return "RESCUE";
    case 7: return "MISSILE";
    case 8: return "SUB";
    case 9: return "QUAL";
    case 125: return "FORMATION";
    case 126: case 127: return "DEMO";
    default: return "";
  }
}

// Version 1 files start with three native-endian uint32_t values: magic,
// version, size of the following raw Amiga RAM dump.  RAM is big-endian.
inline bool describe(const fs::path& path, std::string& label) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return false;
  auto length = f.tellg();
  if (length < std::streamoff(12)) return false;
  uint32_t hdr[3]{};
  if (!read_exact(f, 0, hdr, sizeof hdr) || hdr[0] != 0x46413138u || hdr[1] != 1 ||
      hdr[2] > 8u * 1024u * 1024u || uint64_t(length) < 12u + hdr[2])
    return false;

  auto u8 = [&](uint32_t a) -> int {
    uint8_t b = 0;
    return a < hdr[2] && read_exact(f, 12u + a, &b, 1) ? b : -1;
  };
  auto u16 = [&](uint32_t a) -> int {
    int hi = u8(a), lo = u8(a + 1);
    return hi < 0 || lo < 0 ? -1 : (hi << 8) | lo;
  };
  auto u32 = [&](uint32_t a) -> int32_t {
    uint8_t b[4]{};
    if (a + 4 > hdr[2] || !read_exact(f, 12u + a, b, 4)) return 0;
    return int32_t((uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) |
                   (uint32_t(b[2]) << 8) | uint32_t(b[3]));
  };

  int mission = u8(0x5ab9e);
  int flight = u8(0x5aa8d);
  if (mission < 0 || flight < 0) {
    label = "Saved game";  // old / early-start state with a smaller RAM heap
    return true;
  }
  if (!flight) {
    label = "Menu / briefing";
    return true;
  }

  char s[128];
  int off = int16_t(u16(0x5abd6));
  if (off < 0 || off >= 16 * 512 || (off % 512) != 0 ||
      0x5b47cu + uint32_t(off) + 114u > hdr[2]) {
    std::snprintf(s, sizeof s, "M%d %s  in flight", mission, mission_name(mission));
  } else {
    uint32_t own = 0x5b47cu + uint32_t(off);
    int cx = int16_t(u16(own + 6)), cz = int16_t(u16(own + 8));
    int ft = (u32(own + 24) >> 10) * 5;
    int kt = std::abs(int16_t(u16(own + 110))) / 12;
    int kills = u8(0x5aba3), targets = u8(0x5aba2);
    std::snprintf(s, sizeof s, "M%d %s C%d,%d %dFT %dKT %d/%d",
                  mission, mission_name(mission), cx, cz, ft, kt, kills, targets);
  }
  label = s;
  return true;
}

inline std::vector<Entry> list(const fs::path& dir) {
  std::vector<Entry> entries;
  std::error_code ec;
  fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
  for (; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || it->path().extension() != ".sav") continue;
    std::string label;
    if (!describe(it->path(), label)) continue;
    auto modified = it->last_write_time(ec);
    if (ec) { ec.clear(); modified = fs::file_time_type::min(); }
    entries.push_back({it->path(), std::move(label), modified});
  }
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    return a.modified == b.modified ? a.path.filename() > b.path.filename()
                                    : a.modified > b.modified;
  });
  return entries;
}

// Millisecond timestamp plus a collision suffix: unlimited saves and no
// reuse of the old state1..state4 slots.  Path selection never removes files.
inline fs::path new_path(const fs::path& dir) {
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  for (uint64_t i = 0;; ++i) {
    fs::path p = dir / ("save-" + std::to_string(ms) + "-" + std::to_string(i) + ".sav");
    std::error_code ec;
    if (!fs::exists(p, ec)) return p;
  }
}

}  // namespace savestate_browser
