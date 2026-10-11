#include "amiga_hunk.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <map>

static const std::map<uint32_t, const char*> kHunkNames = {
  {0x3E7, "HUNK_UNIT"}, {0x3E8, "HUNK_NAME"}, {0x3E9, "HUNK_CODE"},
  {0x3EA, "HUNK_DATA"}, {0x3EB, "HUNK_BSS"},  {0x3EC, "HUNK_RELOC32"},
  {0x3ED, "HUNK_RELOC16"}, {0x3EE, "HUNK_RELOC8"}, {0x3EF, "HUNK_EXT"},
  {0x3F0, "HUNK_SYMBOL"}, {0x3F1, "HUNK_DEBUG"}, {0x3F2, "HUNK_END"},
  {0x3F3, "HUNK_HEADER"}, {0x3F5, "HUNK_OVERLAY"}, {0x3F6, "HUNK_BREAK"},
};

std::string hunk_type_name(uint32_t t) {
  auto it = kHunkNames.find(t & 0xFFFF);
  if (it != kHunkNames.end()) return it->second;
  return "UNKNOWN";
}

static uint32_t read_be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8)  | uint32_t(p[3]);
}

bool AmigaExecutable::load(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  f.seekg(0, std::ios::end);
  size_t sz = f.tellg();
  f.seekg(0);
  data.resize(sz);
  f.read(reinterpret_cast<char*>(data.data()), sz);
  if (!f) return false;

  // Scan for known hunk markers (the file uses overlays; full sequential
  // parse of the overlay table is future work).
  hunks.clear();
  for (size_t i = 0; i + 8 <= data.size(); i += 4) {
    uint32_t t = read_be32(&data[i]);
    uint32_t tt = t & 0xFFFF;
    if (tt == 0x3E9 || tt == 0x3EA) { // CODE or DATA
      if (i + 8 > data.size()) continue;
      uint32_t size = read_be32(&data[i + 4]);
      if (size > 0 && size < 200000 && i + 8 + size * 4u <= data.size()) {
        HunkInfo hi;
        hi.type = t;
        hi.size_longs = size;
        hi.file_offset = i + 8;
        hunks.push_back(hi);
      }
    }
  }
  return true;
}

void AmigaExecutable::dump_summary() const {
  std::cout << "Amiga executable: " << data.size() << " bytes, "
            << hunks.size() << " CODE/DATA hunks found\n";
  size_t total_code = 0, total_data = 0;
  for (const auto& h : hunks) {
    if ((h.type & 0xFFFF) == 0x3E9) total_code += h.size_longs * 4;
    else total_data += h.size_longs * 4;
  }
  std::cout << "  Approx CODE: " << total_code << " bytes, DATA: "
            << total_data << " bytes\n";
  // Print first few
  for (size_t i = 0; i < std::min(hunks.size(), size_t(8)); ++i) {
    const auto& h = hunks[i];
    std::cout << "  [" << i << "] " << hunk_type_name(h.type)
              << " @" << std::hex << h.file_offset << std::dec
              << " size=" << h.size_longs * 4 << " B\n";
  }
  if (hunks.size() > 8)
    std::cout << "  ... and " << (hunks.size() - 8) << " more\n";
}
