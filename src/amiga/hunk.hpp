// Amiga hunk executable parser (mirrors tools/hunks.py).
#pragma once

#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace amiga {

// Address the first hunk's 8-byte LoadSeg header is placed at.
// Keep in sync with tools/loadseg.py.
constexpr uint32_t LOAD_BASE = 0x00020000;

struct Hunk {
  enum Kind { Code, Data, Bss } kind = Code;
  uint32_t alloc_size = 0;
  std::vector<uint8_t> data;                        // alloc_size bytes
  std::map<uint32_t, std::vector<uint32_t>> relocs;  // target hunk → offsets
  std::vector<std::pair<std::string, uint32_t>> symbols;
};

struct HunkFile {
  std::vector<Hunk> hunks;

  bool load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)), {});
    return parse(raw);
  }

  bool parse(const std::vector<uint8_t>& raw) {
    size_t pos = 0;
    auto u32 = [&]() -> uint32_t {
      if (pos + 4 > raw.size()) throw 0;
      uint32_t v = uint32_t(raw[pos]) << 24 | uint32_t(raw[pos + 1]) << 16 |
                   uint32_t(raw[pos + 2]) << 8 | raw[pos + 3];
      pos += 4;
      return v;
    };
    try {
      if (u32() != 0x3F3) return false;
      while (uint32_t n = u32()) pos += n * 4;
      u32();  // table size
      uint32_t first = u32(), last = u32();
      hunks.resize(last - first + 1);
      for (auto& h : hunks) {
        uint32_t v = u32();
        if ((v >> 30) == 3) u32();
        h.alloc_size = (v & 0x3FFFFFFF) * 4;
        h.data.assign(h.alloc_size, 0);
      }
      size_t cur = 0;
      while (pos < raw.size()) {
        uint32_t t = u32() & 0x3FFFFFFF;
        Hunk& h = hunks[cur < hunks.size() ? cur : hunks.size() - 1];
        switch (t) {
          case 0x3E9: case 0x3EA: {
            h.kind = t == 0x3E9 ? Hunk::Code : Hunk::Data;
            uint32_t n = u32() * 4;
            for (uint32_t i = 0; i < n && i < h.alloc_size; ++i) h.data[i] = raw[pos + i];
            pos += n;
            break;
          }
          case 0x3EB: h.kind = Hunk::Bss; u32(); break;
          case 0x3EC:
            while (uint32_t n = u32()) {
              uint32_t target = u32();
              for (uint32_t i = 0; i < n; ++i) h.relocs[target].push_back(u32());
            }
            break;
          case 0x3F0:
            while (uint32_t n = u32()) {
              std::string name(reinterpret_cast<const char*>(&raw[pos]), n * 4);
              name.resize(name.find('\0') == std::string::npos ? name.size() : name.find('\0'));
              pos += n * 4;
              h.symbols.push_back({name, u32()});
            }
            break;
          case 0x3F1: pos += u32() * 4; break;
          case 0x3E8: pos += u32() * 4; break;
          case 0x3F2: ++cur; break;
          default: return false;
        }
      }
    } catch (int) {
      return false;
    }
    return true;
  }
};

}  // namespace amiga
