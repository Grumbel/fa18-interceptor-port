// Native pixel-exact reproduction of the fuel LCD at inst5 (104,183)..(122,187).
// The original hardware draws this text into the shared Amiga bitplanes.
// We generate the glyphs from the player record instead. Until we recover
// the original instrument refresh cadence, do not replace a stale LCD frame:
// a mismatch keeps the authentic framebuffer for that presentation.
#include "native/cockpit_instruments.hpp"

#include "amiga/machine.hpp"
#include "native/cockpit_assets.hpp"
#include "native/remaster.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace remaster {
namespace {

// Five rows of three pixels, bit 2 = left. Font reconstructed from the
// five-digit fuel counter in the original 320x200 display (not a font asset).
constexpr uint8_t digit[10][5] = {
  {7, 5, 5, 5, 7},  // 0
  {2, 2, 2, 2, 2},  // 1
  {7, 1, 7, 4, 7},  // 2
  {7, 1, 7, 1, 7},  // 3
  {5, 5, 7, 1, 1},  // 4
  {7, 4, 7, 1, 7},  // 5
  {7, 4, 7, 5, 7},  // 6
  {7, 1, 1, 1, 1},  // 7
  {7, 5, 7, 5, 7},  // 8
  {7, 5, 7, 1, 7},  // 9
};

}  // namespace

FuelDigits prepare_fuel_digits(amiga::Machine& m, const CockpitAtlas& cockpit,
                               const uint8_t* screen_index, const uint32_t* screen_rgb,
                               const uint32_t* row_palette, bool enabled) {
  FuelDigits out;
  if (const char* mode = std::getenv("FA18_COCKPIT_FUEL_NATIVE");
      mode && mode[0] == '0' && mode[1] == '\0') return out;
  if (!enabled || !screen_index || !screen_rgb || !row_palette ||
      m.r8(0x5aa7d) != 0 ||
      (m.r8(0x5aa8d) == 0 && !pause_screen_active())) return out;

  const int16_t player_offset = int16_t(m.r16(0x5abd6));
  if (player_offset < 0 || player_offset >= 16 * 512 || player_offset % 512 != 0)
    return out;
  const int32_t fuel = int32_t(m.r32(0x5b47c + uint32_t(player_offset) + 114)) >> 8;
  if (fuel < 0 || fuel > 99999) return out;
  out.value = uint32_t(fuel);
  if (std::getenv("FA18_COCKPIT_FUEL_DEBUG")) {
    static bool reported_candidate = false;
    if (!reported_candidate) {
      reported_candidate = true;
      std::fprintf(stderr, "cockpit: fuel candidate frame %llu value %05u\n",
                   (unsigned long long)m.frame_count(), out.value);
    }
  }

  // The original inst5 digit area has a black pen (index 16) and a gray
  // LCD fill (index 24). The live LCD uses their low four bits (0 and 8):
  // the game's screen drawing does not preserve the ILBM's EHB bit. Keep
  // the live copper colours instead of hardcoded black/gray RGB.
  constexpr size_t row = size_t(FuelDigits::Y) * CockpitAtlas::W;
  const uint8_t foreground = cockpit.index[row + FuelDigits::X] & 15;
  const uint8_t background = cockpit.index[row + FuelDigits::X - 1] & 15;
  if (!cockpit.valid[row + FuelDigits::X] || !cockpit.valid[row + FuelDigits::X - 1] ||
      foreground == background) return out;

  int digits[5];
  uint32_t n = out.value;
  for (int i = 4; i >= 0; --i) {
    digits[i] = n % 10;
    n /= 10;
  }
  for (int y = 0; y < FuelDigits::H; ++y) {
    const int sy = FuelDigits::Y + y;
    for (int x = 0; x < FuelDigits::W; ++x) {
      const int sx = FuelDigits::X + x;
      const bool ink = (x % 4 < 3) &&
                       (digit[digits[x / 4]][y] & (4 >> (x % 4)));
      const uint8_t index = ink ? foreground : background;
      const uint32_t colour = row_palette[size_t(sy) * 32 + index];
      const size_t at = size_t(y) * FuelDigits::W + x;
      out.rgba[at] = colour;
      // If the game hasn't refreshed the LCD at this point in the physics
      // cycle, keep its earlier digits rather than introducing a new timebase.
      const size_t screen_at = size_t(sy) * CockpitAtlas::W + sx;
      if (screen_index[screen_at] != index && screen_rgb[screen_at] != colour) {
        if (std::getenv("FA18_COCKPIT_FUEL_DEBUG")) {
          static int reported_mismatch_stage = 0;
          const int stage = m.frame_count() >= 4000 ? 3 :
                            m.frame_count() >= 3500 ? 2 : 1;
          if (stage > reported_mismatch_stage) {
            reported_mismatch_stage = stage;
            std::fprintf(stderr,
                         "cockpit: fuel LCD awaiting redraw at frame %llu: value=%05u "
                         "pixel=%d,%d idx=%u expected=%u rgb=%08x expected=%08x\n",
                         (unsigned long long)m.frame_count(), out.value, sx, sy,
                         screen_index[screen_at], index, screen_rgb[screen_at], colour);
          }
        }
        return out;
      }
    }
  }

  out.active = true;
  if (std::getenv("FA18_COCKPIT_FUEL_DEBUG")) {
    static bool reported = false;
    if (!reported) {
      reported = true;
      std::fprintf(stderr, "cockpit: native fuel LCD %05u at (%d,%d), %dx%d\n",
                   out.value, FuelDigits::X, FuelDigits::Y, FuelDigits::W, FuelDigits::H);
    }
  }
  return out;
}

}  // namespace remaster
