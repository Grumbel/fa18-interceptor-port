// Read-only original-blitter write attribution for moving cockpit instruments.
// The blitter writes a 16-bit word in one of four separate 320x200 bitplanes.
// Map its exact destination bits back to source pixels and the 68000 BLTSIZE
// instruction that launched the blit; no emulated memory is changed here.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

namespace amiga::panel_blit_trace {

constexpr int W = 320, H = 200, ROW_BYTES = W / 8;
constexpr uint32_t PLANE_BYTES = uint32_t(ROW_BYTES * H);

struct Zone {
  const char* name;
  int x0, y0, x1, y1;  // Half-open source-space bounds.
};

inline constexpr std::array<Zone, 3> zones{{
  {"left-attitude", 34, 146, 101, 194},
  {"globe", 189, 146, 230, 194},
  {"status", 284, 158, 320, 199},
}};

struct Key {
  uint32_t pc = 0, bank = 0;
  uint8_t zone = 0, plane = 0, minterm = 0;
  bool line = false;

  auto tie() const { return std::tie(pc, bank, zone, plane, minterm, line); }
  bool operator<(const Key& other) const { return tie() < other.tie(); }
};

struct Site {
  uint64_t words = 0, changed_words = 0, changed_bits = 0;
  int min_x = W, min_y = H, max_x = -1, max_y = -1;
  uint32_t first_address = 0;
  uint64_t first_frame = 0, last_frame = 0;
};

class Trace {
public:
  // Accept a verified base of bitplane 0, from the screen-pointer table or
  // display scanout. A ring of past/current bases covers alternating banks.
  void observe_bank(uint32_t base) {
    if (!base || (base & 1) || base > 0x800000u - PLANE_BYTES * 4) return;
    if (std::find(banks_.begin(), banks_.end(), base) != banks_.end()) return;
    banks_[next_bank_++ % banks_.size()] = base;
  }

  void write(uint32_t pc, uint32_t address, uint16_t before, uint16_t after,
             bool line, uint8_t minterm, uint64_t frame) {
    if (address & 1) return;
    for (const uint32_t bank : banks_) {
      if (!bank || address < bank || address - bank >= PLANE_BYTES * 4) continue;
      const uint32_t delta = address - bank;
      const int plane = int(delta / PLANE_BYTES);
      const uint32_t offset = delta % PLANE_BYTES;
      const int y = int(offset / ROW_BYTES);
      const int x = int((offset % ROW_BYTES) * 8);
      const uint16_t changed = before ^ after;
      for (std::size_t z = 0; z < zones.size(); ++z) {
        const Zone& zone = zones[z];
        if (y < zone.y0 || y >= zone.y1 || x >= zone.x1 || x + 16 <= zone.x0)
          continue;
        uint16_t bits = 0;
        for (int i = 0; i < 16; ++i)
          if (x + i >= zone.x0 && x + i < zone.x1)
            bits |= uint16_t(0x8000u >> i);
        if (!bits) continue;
        const Key key{pc, bank, uint8_t(z), uint8_t(plane), minterm, line};
        Site& site = sites_[key];
        if (!site.words) {
          site.first_address = address;
          site.first_frame = frame;
        }
        ++site.words;
        site.last_frame = frame;
        const uint16_t ink = changed & bits;
        if (!ink) continue;
        ++site.changed_words;
        site.changed_bits += std::popcount(unsigned(ink));
        for (int i = 0; i < 16; ++i) {
          if (!(ink & (0x8000u >> i))) continue;
          site.min_x = std::min(site.min_x, x + i);
          site.min_y = std::min(site.min_y, y);
          site.max_x = std::max(site.max_x, x + i);
          site.max_y = std::max(site.max_y, y);
        }
      }
      // A destination word belongs to at most one observed bank.
      return;
    }
  }

  using Entry = std::pair<Key, Site>;
  std::vector<Entry> ranked() const {
    std::vector<Entry> out(sites_.begin(), sites_.end());
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
      if (a.second.changed_bits != b.second.changed_bits)
        return a.second.changed_bits > b.second.changed_bits;
      if (a.second.words != b.second.words) return a.second.words > b.second.words;
      return a.first < b.first;
    });
    return out;
  }

  std::size_t site_count() const { return sites_.size(); }

private:
  std::array<uint32_t, 8> banks_{};
  std::size_t next_bank_ = 0;
  std::map<Key, Site> sites_;
};

}  // namespace amiga::panel_blit_trace
