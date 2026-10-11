#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

// Minimal Amiga Hunk parser for the FA-18 executable.
// The binary uses a complex overlay structure; this is a starting point.

struct HunkInfo {
  uint32_t type;       // e.g. 0x3E9 = CODE
  uint32_t size_longs; // size in longwords
  size_t file_offset;  // offset of the data following the size field
  std::string name;    // optional
};

struct AmigaExecutable {
  std::vector<uint8_t> data;
  std::vector<HunkInfo> hunks;
  bool load(const std::string& path);
  void dump_summary() const;
};

std::string hunk_type_name(uint32_t t);
