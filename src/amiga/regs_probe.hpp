#pragma once

// Pure-RAM register-entry probe, configured by FA18_REGS. Reading a raw
// snapshot never invokes the emulated bus or touches custom-chip registers.
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace amiga {

struct RegsMemoryField {
  uint32_t address = 0;
  uint8_t width = 2;
};

struct RegsProbe {
  uint32_t pc = 0;
  int budget = 20;
  std::vector<RegsMemoryField> fields;
};

inline bool probe_number(std::string_view token, unsigned base, uint32_t& out) {
  if (base == 16 && !token.empty() && token.front() == '$') token.remove_prefix(1);
  if (base == 16 && token.size() >= 2 && token[0] == '0' &&
      (token[1] == 'x' || token[1] == 'X')) token.remove_prefix(2);
  if (token.empty()) return false;
  const char* start = token.data();
  auto [end, ec] = std::from_chars(start, start + token.size(), out, int(base));
  return ec == std::errc{} && end == start + token.size();
}

// Syntax: pc[,count[,addr[:b|w|l]...]]
// Examples: 44686,20,5a9de:w,5a9e0:w,5ac4c:w,5ac4e:w
//           0x44686,5,$5ac4f:b
// Count is decimal; all addresses are hexadecimal. Invalid requests fail
// atomically, instead of silently logging a misleading subset of fields.
inline std::optional<RegsProbe> parse_regs_probe(std::string_view spec,
                                                 uint32_t ram_size) {
  RegsProbe probe;
  int index = 0;
  while (true) {
    size_t comma = spec.find(',');
    std::string_view token = spec.substr(0, comma);
    if (index == 0) {
      if (!probe_number(token, 16, probe.pc) || probe.pc == 0 || (probe.pc & 1))
        return std::nullopt;
    } else if (index == 1) {
      uint32_t count = 0;
      if (!probe_number(token, 10, count) || count > 100000) return std::nullopt;
      probe.budget = int(count);
    } else {
      if (probe.fields.size() == 16) return std::nullopt;
      size_t colon = token.find(':');
      std::string_view addr = token.substr(0, colon);
      char width = 'w';
      if (colon != std::string_view::npos) {
        if (colon + 2 != token.size()) return std::nullopt;
        width = token[colon + 1];
      }
      uint8_t bytes = width == 'b' ? 1 : width == 'w' ? 2 : width == 'l' ? 4 : 0;
      uint32_t address = 0;
      if (!bytes || !probe_number(addr, 16, address) ||
          address >= ram_size || uint32_t(bytes) > ram_size - address)
        return std::nullopt;
      probe.fields.push_back({address, bytes});
    }
    ++index;
    if (comma == std::string_view::npos) break;
    spec.remove_prefix(comma + 1);
  }
  return probe;
}

// Amiga 68000 stores words/longwords big endian. Bounds were checked by
// parse_regs_probe; the caller supplies the emulated RAM snapshot.
inline uint32_t probe_value(const uint8_t* ram, RegsMemoryField field) {
  uint32_t value = 0;
  for (int i = 0; i < field.width; ++i) value = (value << 8) | ram[field.address + uint32_t(i)];
  return value;
}

}  // namespace amiga
