#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

// Minimal Amiga Hunk tools for the FA-18 Interceptor executable.
// The binary is a multi-hunk overlay executable (not a simple single-hunk pack).

struct HunkInfo {
  uint32_t type;       // e.g. 0x3E9 = CODE, 0x3EA = DATA
  uint32_t size_longs; // size in longwords
  size_t   file_offset;// offset of the payload (after type+size)
  std::string name;    // optional (from HUNK_NAME / symbols)
};

struct AmigaExecutable {
  std::vector<uint8_t> data;
  std::vector<HunkInfo> code_data_hunks; // CODE + DATA only for now

  bool load(const std::string& path);
  void dump_summary() const;

  // Extract printable ASCII strings of length >= min_len
  std::vector<std::string> extract_strings(size_t min_len = 5) const;
};

std::string hunk_type_name(uint32_t t);
