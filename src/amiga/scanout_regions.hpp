// Read-only, source-coordinate masks for inspecting original Amiga bitplane
// dependencies. The masks are applied after DMA fetches: they must never
// alter chip RAM, blitter writes, copper colours or bitplane pointer advances.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace amiga {

enum class ScanoutRegion : uint8_t {
  None, UpperScreen, FrontCockpit, LowerPanel, LeftAttitude,
  Fuel, Radar, Globe, Readouts, Status, Count
};

struct ScanoutRegionInfo {
  std::string_view name;
  int x0, y0, x1, y1;  // Half-open 320x200 Amiga source-pixel bounds.
};

inline constexpr std::array<ScanoutRegionInfo, std::size_t(ScanoutRegion::Count)> scanout_regions{{
  {"none", 0, 0, 0, 0},
  {"upper-screen", 0, 0, 320, 133},
  {"front-cockpit", 0, 133, 320, 145},
  {"lower-panel", 0, 145, 320, 200},
  {"left-attitude", 34, 146, 101, 194},
  {"fuel", 102, 153, 125, 195},
  {"radar", 134, 146, 185, 185},
  {"globe", 189, 146, 230, 194},
  {"readouts", 230, 150, 284, 195},
  {"status", 284, 158, 320, 199},
}};

inline constexpr const ScanoutRegionInfo& scanout_region_info(ScanoutRegion region) {
  return scanout_regions[std::size_t(region)];
}

inline constexpr bool scanout_region_contains(ScanoutRegion region, int x, int y) {
  const auto& r = scanout_region_info(region);
  return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1;
}

inline std::optional<ScanoutRegion> parse_scanout_region(std::string_view name) {
  for (std::size_t i = 0; i < scanout_regions.size(); ++i)
    if (scanout_regions[i].name == name) return ScanoutRegion(i);
  return std::nullopt;
}

inline constexpr ScanoutRegion next_scanout_region(ScanoutRegion region) {
  return ScanoutRegion((std::size_t(region) + 1) % scanout_regions.size());
}

}  // namespace amiga
