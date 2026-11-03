// Native pixel-exact reproduction of the fuel LCD at inst5 (104,183)..(122,187).
// The original hardware draws this text into double-buffered bitplanes.
// The original digit writer tells us when each buffer's LCD was refreshed;
// the glyphs themselves are synthesized from the player record.
#include "native/cockpit_instruments.hpp"

#include "amiga/machine.hpp"
#include "native/cockpit_assets.hpp"
#include "native/remaster.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace remaster {
namespace {

constexpr uint32_t kFuelPixelWriter = 0x4744e;
constexpr uint32_t kPlaneBytes = CockpitAtlas::W * CockpitAtlas::H / 8;
constexpr uint32_t kFuelWordOffset = 3 * kPlaneBytes + (FuelDigits::X / 16) * 2;

struct FuelBank {
  uint32_t row_key = 0;
  uint32_t value = 0;
  uint64_t last_latch_frame = UINT64_MAX;
  bool known = false;
  bool refreshed = false;
};

std::array<FuelBank, 4> fuel_banks{};
uint64_t last_frame = 0;

bool debug_enabled() { return std::getenv("FA18_COCKPIT_FUEL_DEBUG") != nullptr; }

int32_t current_fuel(amiga::Machine& m) {
  const int16_t player_offset = int16_t(m.r16(0x5abd6));
  if (player_offset < 0 || player_offset >= 16 * 512 || player_offset % 512 != 0)
    return -1;
  const int32_t fuel = int32_t(m.r32(0x5b47c + uint32_t(player_offset) + 114)) >> 8;
  return (fuel >= 0 && fuel <= 99999) ? fuel : -1;
}

void observe_display_key(uint32_t row_key) {
  if (!row_key) return;
  for (const auto& bank : fuel_banks)
    if (bank.known && bank.row_key == row_key) return;
  for (auto& bank : fuel_banks) {
    if (bank.known) continue;
    bank = FuelBank{};
    bank.row_key = row_key;
    bank.known = true;
    if (debug_enabled())
      std::fprintf(stderr, "cockpit: observed fuel display buffer %06x\n", row_key);
    return;
  }
}

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
  {7, 5, 7, 1, 1},  // 9 (original 3x5 LCD font has no bottom bar)
};

}  // namespace

void reset_cockpit_instrument_state() {
  fuel_banks = {};
  last_frame = 0;
}

void install_cockpit_instrument_hooks(amiga::Machine& m) {
  reset_cockpit_instrument_state();
  // The 68000 writes the first fuel glyph bitplane-3 word at $4744e.
  // Its destination is the first stack argument. Each screen buffer has
  // a distinct row pointer, so a draw into the hidden back buffer does not
  // prematurely update the LCD shown by the other buffer.
  m.add_pc_hook(kFuelPixelWriter, [&m] {
    const uint32_t dst = m.r32(m.cpu.a[7] + 4) & 0x00ffffff;
    for (auto& bank : fuel_banks) {
      if (!bank.known || dst != bank.row_key + kFuelWordOffset) continue;
      if (bank.last_latch_frame == m.frame_count()) return;
      const int32_t fuel = current_fuel(m);
      if (fuel < 0) return;
      bank.value = uint32_t(fuel);
      bank.refreshed = true;
      bank.last_latch_frame = m.frame_count();
      if (debug_enabled())
        std::fprintf(stderr, "cockpit: fuel latch frame %llu buffer=%06x value=%05u\n",
                     (unsigned long long)m.frame_count(), bank.row_key, bank.value);
      return;
    }
  });
}

FuelDigits prepare_fuel_digits(amiga::Machine& m, const CockpitAtlas& cockpit,
                               const uint32_t* row_keys,
                               const uint32_t* row_palette, bool enabled) {
  FuelDigits out;
  if (const char* mode = std::getenv("FA18_COCKPIT_FUEL_NATIVE");
      mode && mode[0] == '0' && mode[1] == '\0') return out;
  if (!enabled || !row_keys || !row_palette ||
      m.r8(0x5aa7d) != 0 ||
      (m.r8(0x5aa8d) == 0 && !pause_screen_active())) return out;

  // State loads can rewind game-frame time without restarting the process.
  if (m.frame_count() < last_frame) reset_cockpit_instrument_state();
  last_frame = m.frame_count();
  const uint32_t displayed_key = row_keys[FuelDigits::Y];
  observe_display_key(displayed_key);
  const FuelBank* displayed = nullptr;
  for (const auto& bank : fuel_banks)
    if (bank.known && bank.row_key == displayed_key && bank.refreshed)
      displayed = &bank;
  // Leave the original layer in place until we have actually seen the
  // original game redraw this particular screen buffer.
  if (!displayed) return out;
  out.value = displayed->value;

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
      const bool ink = (x % 4 < 3) &&
                       (digit[digits[x / 4]][y] & (4 >> (x % 4)));
      const uint8_t index = ink ? foreground : background;
      const uint32_t colour = row_palette[size_t(sy) * 32 + index];
      out.rgba[size_t(y) * FuelDigits::W + x] = colour;
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
