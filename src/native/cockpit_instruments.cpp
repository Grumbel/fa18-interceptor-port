// Native cockpit digits, independent of the Amiga's displayed bitplane pixels.
// The original glyph writers provide precise per-buffer redraw timing; the
// actual shapes come from the player record or the game's resident 3x5 font.
#include "native/cockpit_instruments.hpp"

#include "amiga/machine.hpp"
#include "native/cockpit_assets.hpp"
#include "native/hud_glyph.hpp"
#include "native/remaster.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>

namespace remaster {
namespace {

constexpr uint32_t kFuelPixelWriter = 0x4744e;
constexpr uint32_t kRightGlyphWriter = 0x47476;
constexpr uint32_t kHudGlyphWriter = 0x47d56;
constexpr uint32_t kHudPointWriter = 0x444c0;
constexpr uint32_t kRadarPen5Writer = 0x4446a;
constexpr uint32_t kReticleVerticalWriter = 0x444a2;
constexpr uint32_t kReticleHorizontalWriter = 0x44924;
constexpr uint32_t kPlaneBytes = CockpitAtlas::W * CockpitAtlas::H / 8;
constexpr uint32_t kFuelWordOffset = 3 * kPlaneBytes + (FuelDigits::X / 16) * 2;
constexpr uint32_t kRowStride = CockpitAtlas::W / 8;

struct CapturedGlyph {
  int8_t value = -1;
  int8_t pending = -1;
  uint8_t rows = 0;
  uint32_t source_base = 0;
  bool complete = false;
};

struct HeadingBank {
  uint64_t drawing_frame = UINT64_MAX;
  hud_font::HeadingMarkWrites writes{};
  int phase = -1;
};

struct ReticleBank {
  uint64_t drawing_frame = UINT64_MAX;
  hud_font::ReticleWrites writes{};
  bool ready = false;
};

struct RadarBank {
  int16_t dot_x = -1;
  int16_t dot_y = -1;
  uint64_t drawing_frame = UINT64_MAX;
  bool captured = false;
};

struct CapturedReadout {
  uint16_t x = 0, y = 0;
  uint8_t ink_pen = 0, background_pen = 0;
  uint8_t rows = 0;
  uint32_t source_base = 0;
  int8_t digit = -1;
  bool complete = false;
};

struct FuelBank {
  uint32_t row_key = 0;
  uint32_t value = 0;
  uint64_t last_latch_frame = UINT64_MAX;
  bool known = false;
  bool refreshed = false;
  std::array<uint8_t, FlightReadouts::W * FlightReadouts::H> right_valid{};
  std::array<uint8_t, FlightReadouts::W * FlightReadouts::H> right_pen{};
  bool right_refreshed = false;
  std::array<CapturedReadout, 64> right_digits{};
  uint8_t right_digit_count = 0;
  std::array<CapturedGlyph, kHudDigitSlots.size()> hud_digits{};
  std::array<CapturedGlyph, kHudLetterSlots.size()> hud_letters{};
  HeadingBank heading{};
  ReticleBank reticle{};
  RadarBank radar{};
};

std::array<FuelBank, 4> fuel_banks{};
uint64_t last_frame = 0;

bool debug_enabled() { return std::getenv("FA18_COCKPIT_FUEL_DEBUG") != nullptr; }

void capture_glyph_row(CapturedGlyph& glyph, int row, uint32_t base, int value,
                       uint64_t frame, uint32_t bank_key, int x, int y) {
  if (row == 0) {
    glyph.complete = false;
    glyph.pending = int8_t(value);
    glyph.source_base = base;
    glyph.rows = 0;
  }
  if (glyph.source_base != base || glyph.pending < 0 ||
      glyph.rows != row) return;
  ++glyph.rows;
  if (glyph.rows == 5) {
    glyph.value = glyph.pending;
    glyph.complete = true;
    if (std::getenv("FA18_HUD_CAPTURE_DEBUG")) {
      static int printed = 0;
      if (printed++ < 48)
        std::fprintf(stderr, "HUD captured font frame=%llu bank=%06x x=%d y=%d value=%d\n",
                     (unsigned long long)frame, bank_key, x, y, value);
    }
  }
}

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

// The original LCD blitters deliver one row per hook call. Record a digit
// only after all five consecutive source rows were written to this screen
// bank; never infer a newly drawn number from a partly updated bitmap.
void capture_readout(FuelBank& bank, amiga::Machine& m, int x, int top,
                     int row, uint32_t source, uint8_t ink, uint8_t background) {
  CapturedReadout* found = nullptr;
  for (int i = 0; i < bank.right_digit_count; ++i) {
    auto& candidate = bank.right_digits[std::size_t(i)];
    if (candidate.x == x && candidate.y == top) {
      found = &candidate;
      break;
    }
  }
  if (!found) {
    if (row != 0 || bank.right_digit_count == bank.right_digits.size()) return;
    found = &bank.right_digits[bank.right_digit_count++];
    found->x = uint16_t(x);
    found->y = uint16_t(top);
  }
  if (row == 0) {
    uint8_t bytes[5]{};
    for (int i = 0; i < 5; ++i) bytes[i] = m.r8(source + uint32_t(i));
    found->digit = int8_t(hud_font::source_digit(bytes));
    found->rows = 0;
    found->source_base = source;
    found->ink_pen = ink;
    found->background_pen = background;
    found->complete = false;
  }
  if (found->source_base != source || found->rows != row ||
      found->ink_pen != ink || found->background_pen != background) return;
  if (++found->rows == 5) found->complete = found->digit >= 0;
}

}  // namespace

void reset_cockpit_instrument_state() {
  fuel_banks = {};
  last_frame = 0;
}

void install_cockpit_instrument_hooks(amiga::Machine& m) {
  reset_cockpit_instrument_state();
  // The four-plane pixel routine for palette pen 5 starts at $4446a:
  // OR.W D4,(A3), AND.W D1,(A2), OR.W D6,(A1), AND.W D3,(A0).
  // Its plane-0 destination and single-bit D4 mask give the exact contact
  // coordinate in whichever Amiga screen buffer the game is redrawing.
  m.add_pc_hook(kRadarPen5Writer, [&m] {
    const uint16_t mask = uint16_t(m.cpu.d[4]);
    if (!std::has_single_bit(mask)) return;
    const uint32_t dst = m.cpu.a[3] & 0x00ffffff;
    for (auto& bank : fuel_banks) {
      if (!bank.known || bank.row_key < FuelDigits::Y * kRowStride) continue;
      const uint32_t plane0 = bank.row_key - FuelDigits::Y * kRowStride;
      if (dst < plane0 || dst >= plane0 + kPlaneBytes) continue;
      const uint32_t pos = dst - plane0;
      const int y = int(pos / kRowStride);
      const int x = int((pos % kRowStride) * 8 + 15 - std::countr_zero(mask));
      if (x < RadarSymbols::X || x >= RadarSymbols::X + RadarSymbols::W ||
          y < RadarSymbols::Y || y >= RadarSymbols::Y + RadarSymbols::H) continue;
      bank.radar = RadarBank{int16_t(x), int16_t(y), m.frame_count(), true};
      if (std::getenv("FA18_RADAR_WRITER_DEBUG"))
        std::fprintf(stderr, "radar writer frame=%llu bank=%06x x=%d y=%d mask=%04x a3=%06x\n",
                     (unsigned long long)m.frame_count(), bank.row_key, x, y, mask, dst);
      break;
    }
  });
  // The 68000 writes the first fuel glyph bitplane-3 word at $4744e.
  // Its destination is the first stack argument. Each screen buffer has
  // a distinct row pointer, so a draw into the hidden back buffer does not
  // prematurely update the LCD shown by the other buffer.
  m.add_pc_hook(kFuelPixelWriter, [&m] {
    const uint32_t dst = m.r32(m.cpu.a[7] + 4) & 0x00ffffff;
    // The same writer that updates the fuel LCD also draws the two ECM
    // coordinate numbers. The ECM text clears pen bits 0 and 2 to form
    // black 3x5 glyphs against the panel's purple pen 5. Capture the font
    // row on the plane-2 pass, after its companion plane-0 pass, retaining
    // the original screen-buffer selection and scanline palette.
    const uint32_t glyph_base = m.cpu.d[4] & 0x00ffffff;
    const uint32_t glyph_byte = m.cpu.a[0] & 0x00ffffff;
    if (glyph_base >= 0x52900 && glyph_base < 0x52b00 &&
        glyph_byte >= glyph_base && glyph_byte - glyph_base < 5) {
      const int glyph_row = int(glyph_byte - glyph_base);
      for (auto& bank : fuel_banks) {
        if (!bank.known || bank.row_key < FuelDigits::Y * kRowStride) continue;
        const uint32_t plane2 = bank.row_key - FuelDigits::Y * kRowStride +
                                2 * kPlaneBytes;
        if (dst < plane2 || dst >= plane2 + kPlaneBytes) continue;
        const uint32_t offset = dst - plane2;
        const int top = int(offset / kRowStride);
        const int x = int((offset % kRowStride) * 8 + (m.cpu.d[2] & 15));
        if ((top != 180 && top != 188) || x < 292 || x + 2 > 307) continue;
        const uint8_t pattern = m.r8(glyph_byte);
        const int y = top + glyph_row;
        for (int i = 0; i < 3; ++i) {
          const size_t at = size_t(y - FlightReadouts::Y) * FlightReadouts::W +
                            size_t(x + i - FlightReadouts::X);
          bank.right_valid[at] = 1;
          bank.right_pen[at] = (pattern >> (7 - i)) & 1 ? 0 : 5;
        }
        capture_readout(bank, m, x, top, glyph_row, glyph_base, 0, 5);
        bank.right_refreshed = true;
        if (std::getenv("FA18_COCKPIT_TEXT_DEBUG") && glyph_row == 0)
          std::fprintf(stderr, "cockpit: ECM glyph frame=%llu buffer=%06x x=%d y=%d font=%06x\n",
                       (unsigned long long)m.frame_count(), bank.row_key, x, y, glyph_base);
        break;
      }
    }
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
  // The other branch of the original font blitter ($47476) writes LCD pen
  // 4, including all five green cockpit numerical fields. Capture
  // its *source glyph bytes*, never the destination bitmap. The destination
  // address identifies the Amiga screen bank and exact glyph position.
  m.add_pc_hook(kRightGlyphWriter, [&m] {
    const uint32_t dst = m.cpu.a[3] & 0x00ffffff;
    const uint32_t glyph_base = m.cpu.d[4] & 0x00ffffff;
    const uint32_t glyph_byte = m.cpu.a[0] & 0x00ffffff;
    if (glyph_base < 0x52900 || glyph_base >= 0x52b00 ||
        glyph_byte < glyph_base || glyph_byte - glyph_base >= 5) return;
    const int glyph_row = int(glyph_byte - glyph_base);
    for (auto& bank : fuel_banks) {
      if (!bank.known || bank.row_key < FuelDigits::Y * kRowStride) continue;
      const uint32_t screen_base = bank.row_key - FuelDigits::Y * kRowStride;
      if (dst < screen_base + 2 * kPlaneBytes ||
          dst >= screen_base + 3 * kPlaneBytes) continue;
      const uint32_t offset = dst - screen_base - 2 * kPlaneBytes;
      const int y = int(offset / kRowStride);
      const int x = int((offset % kRowStride) * 8 + (m.cpu.d[2] & 15));
      const int top = y - glyph_row;
      const bool right = (top == 158 || top == 166 || top == 175 || top == 184) &&
                         x >= 240 && x + 2 <= 265;
      const bool left = top == 184 && x >= 144 && x + 2 <= 154;
      if (!right && !left)
        continue;
      const uint8_t pattern = m.r8(glyph_byte);
      for (int i = 0; i < 3; ++i) {
        const size_t at = size_t(y - FlightReadouts::Y) * FlightReadouts::W +
                          size_t(x + i - FlightReadouts::X);
        bank.right_valid[at] = 1;
        bank.right_pen[at] = (pattern >> (7 - i)) & 1 ? 4 : 0;
      }
      capture_readout(bank, m, x, top, glyph_row, glyph_base, 4, 0);
      bank.right_refreshed = true;
      if (std::getenv("FA18_COCKPIT_TEXT_DEBUG") && glyph_row == 0)
        std::fprintf(stderr, "cockpit: instrument glyph frame=%llu buffer=%06x x=%d y=%d font=%06x\n",
                     (unsigned long long)m.frame_count(), bank.row_key, x, y, glyph_base);
      return;
    }
  });
  // The flight HUD uses another branch of the same resident 3x5 font:
  // $47d56 ORs the pre-shifted glyph row into bitplane 3. D4 is the source
  // glyph, A0 its current byte, A3 the double-buffered destination and the
  // low nibble of D2 is the horizontal shift. Record only complete five-row
  // writes to the 17 measured HUD cells, not arbitrary menu lettering.
  m.add_pc_hook(kHudGlyphWriter, [&m] {
    const uint32_t dst = m.cpu.a[3] & 0xffffff;
    const uint32_t source = m.cpu.d[4] & 0xffffff;
    const uint32_t src_row = m.cpu.a[0] & 0xffffff;
    if (source < 0x52900 || source + 5 > 0x52b00 ||
        src_row < source || src_row - source >= 5) return;
    const int row = int(src_row - source);
    if ((m.cpu.d[6] & 0xffff) != uint32_t(4 - row)) return;
    uint8_t bytes[5]{};
    if (row == 0)
      for (int i = 0; i < 5; ++i) bytes[i] = m.r8(source + uint32_t(i));
    for (auto& bank : fuel_banks) {
      if (!bank.known || bank.row_key < FuelDigits::Y * kRowStride) continue;
      const uint32_t plane3 = bank.row_key - FuelDigits::Y * kRowStride +
                              3 * kPlaneBytes;
      if (dst < plane3 || dst >= plane3 + kPlaneBytes) continue;
      const uint32_t pos = dst - plane3;
      const int y = pos / kRowStride;
      const int x = int((pos % kRowStride) * 8 + (m.cpu.d[2] & 15));
      for (size_t i = 0; i < kHudDigitSlots.size(); ++i) {
        const auto& slot = kHudDigitSlots[i];
        if (x != slot.x || y != slot.y + row) continue;
        auto& glyph = bank.hud_digits[i];
        const int value = row == 0 ? hud_font::source_digit(bytes) : glyph.pending;
        capture_glyph_row(glyph, row, source, value, m.frame_count(),
                          bank.row_key, x, slot.y);
        return;
      }
      for (size_t i = 0; i < kHudLetterSlots.size(); ++i) {
        const auto& slot = kHudLetterSlots[i];
        if (x != slot.x || y != slot.y + row) continue;
        auto& glyph = bank.hud_letters[i];
        const int value = row == 0 ? int(hud_font::source_letter(bytes, slot.letter)) :
                                     glyph.pending;
        capture_glyph_row(glyph, row, source, value, m.frame_count(),
                          bank.row_key, x, slot.y);
        return;
      }
    }
  });
  // The heading tape is not font text. The original $444c0 pixel writer
  // receives a one-bit D4 mask and a bitplane-3 word in A0. The heading
  // drawing context (A5=$5a966, pen 5) separates these points from terrain,
  // objects and other screen-space line drawing. Require an exact set of
  // eleven points before accepting the moving tick phase for this buffer.
  m.add_pc_hook(kHudPointWriter, [&m] {
    if (m.cpu.a[5] != 0x5a966 || (m.cpu.d[4] >> 16) != 5) return;
    const uint16_t mask = uint16_t(m.cpu.d[4]);
    if (!std::has_single_bit(mask)) return;
    const uint32_t dst = m.cpu.a[0] & 0x00ffffff;
    for (auto& bank : fuel_banks) {
      if (!bank.known || bank.row_key < FuelDigits::Y * kRowStride) continue;
      const uint32_t plane3 = bank.row_key - FuelDigits::Y * kRowStride +
                              3 * kPlaneBytes;
      if (dst < plane3 || dst >= plane3 + kPlaneBytes) continue;
      const uint32_t pos = dst - plane3;
      const int y = int(pos / kRowStride);
      const int x = int((pos % kRowStride) * 8 + 15 - std::countr_zero(mask));
      if (!((y == 60 || y == 61) && x >= 138 && x <= 188) &&
          !(x == 159 && y >= 63 && y <= 65)) return;
      HeadingBank& heading = bank.heading;
      if (heading.drawing_frame != m.frame_count()) {
        heading.drawing_frame = m.frame_count();
        heading.writes = {};
        heading.phase = -1;
      }
      heading.writes.add(x, y);
      const int phase = heading.writes.phase();
      if (phase >= 0 && heading.phase < 0 &&
          std::getenv("FA18_HUD_MARK_CAPTURE_DEBUG")) {
        static int printed = 0;
        if (printed++ < 40)
          std::fprintf(stderr, "HUD heading captured frame=%llu bank=%06x first=%d\n",
                       (unsigned long long)m.frame_count(), bank.row_key, phase);
      }
      heading.phase = phase;
      return;
    }
  });
  // The reticle is drawn by two branches of the original line rasteriser:
  // $444a2 ORs four single-bit vertical points into plane 3, and $44924
  // writes the centre five-pixel horizontal row across two plane-3 words.
  // Its caller context is distinct from the heading and regular 3D lines.
  // A matching write sequence is a redraw latch for the *destination* bank.
  auto reticle_bank = [](uint32_t plane3, uint32_t destination,
                         uint32_t row_offset) -> bool {
    return destination == plane3 + row_offset;
  };
  m.add_pc_hook(kReticleVerticalWriter, [&m, reticle_bank] {
    if (m.cpu.a[4] != 0x4449c || m.cpu.a[5] != 0xffff252c ||
        m.cpu.d[4] != 0x000a0001) return;
    const uint32_t destination = m.cpu.a[0] & 0xffffff;
    for (auto& bank : fuel_banks) {
      if (!bank.known || bank.row_key < FuelDigits::Y * kRowStride) continue;
      const uint32_t plane3 = bank.row_key - FuelDigits::Y * kRowStride +
                              3 * kPlaneBytes;
      for (int y : {71, 72, 74, 75}) {
        if (!reticle_bank(plane3, destination, uint32_t(y * kRowStride + 18)))
          continue;
        auto& reticle = bank.reticle;
        if (reticle.drawing_frame != m.frame_count()) {
          reticle.drawing_frame = m.frame_count();
          reticle.writes = {};
          reticle.ready = false;
        }
        reticle.writes.add_vertical(y);
        reticle.ready = reticle.writes.complete();
        return;
      }
    }
  });
  m.add_pc_hook(kReticleHorizontalWriter, [&m, reticle_bank] {
    if (m.cpu.a[4] != 0x4449c || m.cpu.a[5] != 0xffff252c ||
        m.cpu.d[4] != 0x000a0142) return;
    const uint32_t destination = m.cpu.d[7] & 0xffffff;
    for (auto& bank : fuel_banks) {
      if (!bank.known || bank.row_key < FuelDigits::Y * kRowStride) continue;
      const uint32_t plane3 = bank.row_key - FuelDigits::Y * kRowStride +
                              3 * kPlaneBytes;
      if (!reticle_bank(plane3, destination, 73 * kRowStride + 19)) continue;
      auto& reticle = bank.reticle;
      if (reticle.drawing_frame != m.frame_count()) {
        reticle.drawing_frame = m.frame_count();
        reticle.writes = {};
        reticle.ready = false;
      }
      reticle.writes.horizontal = true;
      if (!reticle.ready && reticle.writes.complete() &&
          std::getenv("FA18_HUD_RETICLE_CAPTURE_DEBUG")) {
        static int printed = 0;
        if (printed++ < 40)
          std::fprintf(stderr, "HUD reticle captured frame=%llu bank=%06x\n",
                       (unsigned long long)m.frame_count(), bank.row_key);
      }
      reticle.ready = reticle.writes.complete();
      return;
    }
  });
}

HudFontCapture captured_hud_font(const uint32_t* row_keys) {
  HudFontCapture out;
  out.digits.fill(-1);
  if (const char* opt = std::getenv("FA18_HUD_FONT_CAPTURE");
      opt && opt[0] == '0' && opt[1] == '\0') return out;
  if (!row_keys) return out;
  // HUD interception must also work when the separate cockpit-art pass is
  // disabled. Observe the displayed bitmap independently of fuel/readouts.
  observe_display_key(row_keys[FuelDigits::Y]);
  for (const auto& bank : fuel_banks) {
    if (!bank.known || bank.row_key != row_keys[FuelDigits::Y] ||
        bank.row_key < FuelDigits::Y * kRowStride ||
        bank.row_key - FuelDigits::Y * kRowStride != row_keys[0]) continue;
    for (size_t i = 0; i < bank.hud_digits.size(); ++i)
      if (bank.hud_digits[i].complete) out.digits[i] = bank.hud_digits[i].value;
    for (size_t i = 0; i < bank.hud_letters.size(); ++i)
      out.letters[i] = bank.hud_letters[i].complete && bank.hud_letters[i].value == 1;
    break;
  }
  return out;
}

int captured_heading_phase(const uint32_t* row_keys) {
  if (const char* opt = std::getenv("FA18_HUD_MARK_CAPTURE");
      opt && opt[0] == '0' && opt[1] == '\0') return -1;
  if (!row_keys) return -1;
  observe_display_key(row_keys[FuelDigits::Y]);
  for (const auto& bank : fuel_banks) {
    if (!bank.known || bank.row_key != row_keys[FuelDigits::Y] ||
        bank.row_key < FuelDigits::Y * kRowStride ||
        bank.row_key - FuelDigits::Y * kRowStride != row_keys[0]) continue;
    return bank.heading.phase;
  }
  return -1;
}

bool captured_reticle_ready(const uint32_t* row_keys) {
  if (const char* opt = std::getenv("FA18_HUD_RETICLE_CAPTURE");
      opt && opt[0] == '0' && opt[1] == '\0') return false;
  if (!row_keys) return false;
  observe_display_key(row_keys[FuelDigits::Y]);
  for (const auto& bank : fuel_banks) {
    if (!bank.known || bank.row_key != row_keys[FuelDigits::Y] ||
        bank.row_key < FuelDigits::Y * kRowStride ||
        bank.row_key - FuelDigits::Y * kRowStride != row_keys[0]) continue;
    return bank.reticle.ready;
  }
  return false;
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
    out.stroke_masks[i] = hud_font::kDigitStrokes[digits[i]];
    n /= 10;
  }
  out.ink_rgb = row_palette[size_t(FuelDigits::Y) * 32 + foreground] & 0xffffffu;
  // A single native stroke colour is accurate only while all five scanline
  // pens agree. Otherwise keep the existing bitplane-exact LCD until a
  // per-row gradient can be expressed by the vector layer itself.
  const char* hires = std::getenv("FA18_COCKPIT_FUEL_HIRES");
  out.vector_enabled = m.hires_scale >= 2 &&
      (!hires || hires[0] != '0' || hires[1] != '\0');
  for (int y = 0; y < FuelDigits::H; ++y) {
    const int sy = FuelDigits::Y + y;
    out.background_rgba[size_t(y)] = row_palette[size_t(sy) * 32 + background];
    if ((row_palette[size_t(sy) * 32 + foreground] & 0xffffffu) != out.ink_rgb)
      out.vector_enabled = false;
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

FuelBar prepare_fuel_bar(const FuelDigits& displayed_fuel,
                         const uint32_t* row_palette, int hires_scale) {
  FuelBar out;
  if (const char* mode = std::getenv("FA18_COCKPIT_FUEL_BAR_NATIVE");
      mode && mode[0] == '0' && mode[1] == '\0') return out;
  if (!displayed_fuel.active || !row_palette || hires_scale < 2) return out;

  // Replayed quarter/half/three-quarter tanks show respectively 5/11/17
  // filled rows, and 24,998 units shows 22. The original gauge has 23
  // possible rows (y=158..180) and truncates fuel*23/25000. Fuel may be
  // larger than one tank in damaged or edited states; clamp the display.
  out.filled_rows = int(std::min<uint32_t>(FuelBar::H,
                                           displayed_fuel.value * FuelBar::H / 25000));
  out.ink_rgb = row_palette[size_t(FuelBar::Y) * 32 + 4] & 0xffffffu;
  for (int y = 0; y < FuelBar::H; ++y) {
    const int sy = FuelBar::Y + y;
    out.background_rgba[size_t(y)] = row_palette[size_t(sy) * 32];
    // A single vector ink is only valid when the copper palette keeps
    // pen 4 consistent throughout this instrument's scanlines. On unusual
    // palettes/transition frames retain the original bar bitplanes.
    if ((row_palette[size_t(sy) * 32 + 4] & 0xffffffu) != out.ink_rgb)
      return FuelBar{};
  }
  out.active = true;
  return out;
}

RadarSymbols prepare_radar_symbols(amiga::Machine& m, const uint32_t* rgb,
                                   const uint32_t* row_keys, const uint32_t* row_palette,
                                   bool enabled) {
  RadarSymbols out;
  if (const char* mode = std::getenv("FA18_COCKPIT_RADAR_NATIVE");
      mode && mode[0] == '0' && mode[1] == '\0') return out;
  if (!enabled || !rgb || !row_keys || !row_palette || m.hires_scale < 2 ||
      m.r8(0x5aa7d) != 0 ||
      (m.r8(0x5aa8d) == 0 && !pause_screen_active())) return out;

  const auto pixel = [&](int x, int y) { return rgb[std::size_t(y) * 320 + x] & 0xffffffu; };
  const uint32_t guide = pixel(139, 148), marker = pixel(158, 167);
  if (guide == marker || guide == pixel(135, 148) ||
      marker == pixel(135, 167)) return out;
  int guides = 0;
  for (int y = 148; y <= 163; ++y) {
    guides += pixel(139 + y - 148, y) == guide;
    guides += pixel(177 - (y - 148), y) == guide;
  }
  if (guides < 31 || pixel(157, 168) != marker ||
      pixel(158, 168) != marker || pixel(159, 168) != marker) return out;

  // Choose the contact latched by the *displayed* screen buffer. Its pen
  // comes from the original copper palette, not from identifying a blue
  // pixel in the finished image. A single source pixel only confirms that
  // the captured write has reached the display on partial redraw frames.
  observe_display_key(row_keys[FuelDigits::Y]);
  for (const auto& bank : fuel_banks) {
    if (!bank.known || bank.row_key != row_keys[FuelDigits::Y] ||
        !bank.radar.captured) continue;
    out.dot_x = bank.radar.dot_x;
    out.dot_y = bank.radar.dot_y;
    out.dot_rgb = row_palette[std::size_t(out.dot_y) * 32 + 5] & 0xffffffu;
    break;
  }
  if (out.dot_x < 0 || pixel(out.dot_x, out.dot_y) != out.dot_rgb)
    return RadarSymbols{};

  out.guide_rgb = guide;
  out.marker_rgb = marker;
  for (int y = RadarSymbols::Y; y < RadarSymbols::Y + RadarSymbols::H; ++y)
    out.background[std::size_t(y - RadarSymbols::Y)] = rgb[std::size_t(y) * 320 + 135];
  out.active = true;
  return out;
}

FlightReadouts prepare_flight_readouts(amiga::Machine& m, const uint32_t* row_keys,
                                       const uint32_t* row_palette, bool enabled) {
  FlightReadouts out;
  if (const char* mode = std::getenv("FA18_COCKPIT_READOUTS_NATIVE");
      mode && mode[0] == '0' && mode[1] == '\0') return out;
  if (!enabled || !row_keys || !row_palette || m.r8(0x5aa7d) != 0 ||
      (m.r8(0x5aa8d) == 0 && !pause_screen_active())) return out;

  observe_display_key(row_keys[FuelDigits::Y]);
  for (const auto& bank : fuel_banks) {
    if (!bank.known || !bank.right_refreshed ||
        bank.row_key != row_keys[FuelDigits::Y]) continue;
    for (int y = 0; y < FlightReadouts::H; ++y)
      for (int x = 0; x < FlightReadouts::W; ++x) {
        const size_t i = size_t(y) * FlightReadouts::W + size_t(x);
        if (!bank.right_valid[i]) continue;
        out.valid[i] = 1;
        out.rgba[i] = row_palette[size_t(FlightReadouts::Y + y) * 32 +
                                  bank.right_pen[i]];
      }
    const char* hires = std::getenv("FA18_COCKPIT_READOUTS_HIRES");
    if (m.hires_scale >= 2 &&
        (!hires || hires[0] != '0' || hires[1] != '\0')) {
      for (int i = 0; i < bank.right_digit_count; ++i) {
        const auto& g = bank.right_digits[std::size_t(i)];
        if (!g.complete || out.vector_count == out.vectors.size() ||
            g.x < FlightReadouts::X || g.x + 3 > FlightReadouts::X + FlightReadouts::W ||
            g.y < FlightReadouts::Y || g.y + 5 > FlightReadouts::Y + FlightReadouts::H)
          continue;
        // The live readout may have been overwritten since the last complete
        // redraw. Replace it only if *all* source-font rows still agree with
        // the captured digit and both pen identities.
        bool matches = true;
        for (int row = 0; row < 5; ++row)
          for (int col = 0; col < 3; ++col) {
            const size_t at = size_t(g.y + row - FlightReadouts::Y) * FlightReadouts::W +
                              size_t(g.x + col - FlightReadouts::X);
            const bool ink = (hud_font::kDigitRows[g.digit][row] & (4 >> col)) != 0;
            matches &= bank.right_valid[at] != 0 &&
                       bank.right_pen[at] == (ink ? g.ink_pen : g.background_pen) &&
                       out.vector_index[at] == 0;
          }
        if (!matches) continue;
        const uint8_t index = ++out.vector_count;
        out.vectors[std::size_t(index - 1)] = {
          g.x, g.y, hud_font::kDigitStrokes[g.digit], g.ink_pen, g.background_pen
        };
        for (int row = 0; row < 5; ++row)
          for (int col = 0; col < 3; ++col) {
            const size_t at = size_t(g.y + row - FlightReadouts::Y) * FlightReadouts::W +
                              size_t(g.x + col - FlightReadouts::X);
            out.vector_index[at] = index;
          }
      }
    }
    if (std::getenv("FA18_COCKPIT_READOUT_VECTOR_DEBUG")) {
      static bool printed = false;
      if (!printed && out.vector_count > 0) {
        printed = true;
        std::fprintf(stderr, "cockpit: %u complete native high-resolution readout digits\n",
                     unsigned(out.vector_count));
      }
    }
    out.active = true;
    if (std::getenv("FA18_COCKPIT_TEXT_DEBUG")) {
      static bool reported = false;
      if (!reported) {
        reported = true;
        std::fprintf(stderr, "cockpit: native flight and ECM LCD glyphs at (%d,%d), %dx%d\n",
                     FlightReadouts::X, FlightReadouts::Y,
                     FlightReadouts::W, FlightReadouts::H);
      }
    }
    break;
  }
  return out;
}

}  // namespace remaster
