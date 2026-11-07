#include "amiga/panel_blit_trace.hpp"

#include <cassert>

int main() {
  using namespace amiga::panel_blit_trace;
  Trace trace;
  assert(trace.ranked().empty());
  constexpr uint32_t base_a = 0x10000, base_b = 0x24000;
  trace.observe_bank(base_a);
  trace.observe_bank(base_a);  // Duplicate bank is not observed twice.
  trace.observe_bank(base_b);
  trace.observe_bank(0);
  trace.observe_bank(1);
  trace.observe_bank(0xffff0000u);  // Reject an overflowing extent.
  trace.observe_bank(0x7fe000);  // Four planes would exceed RAM.

  // Pixel 200 lies in the word at x=192, bit 7. Changes in the next pixel
  // must contribute only when the word crosses a selected instrument zone.
  const uint32_t address = base_a + PLANE_BYTES * 2 + 170 * ROW_BYTES + 24;
  trace.write(0x447ee, address, 0, 0x0080, true, 0x3c, 10);
  trace.write(0x447ee, address, 0x0080, 0x0080, true, 0x3c, 11);
  trace.write(0x447ee, address, 0x0080, 0, true, 0x3c, 12);
  // A separate displayed bank has a distinct original blitter writer site.
  trace.write(0x447ee, base_b + PLANE_BYTES * 2 + 170 * ROW_BYTES + 24,
              0, 0x0080, true, 0x3c, 13);
  // The same PC may draw a different instrument on a different plane.
  trace.write(0x447ee, base_a + PLANE_BYTES + 170 * ROW_BYTES + 2,
              0, 0xffff, false, 0xf0, 13);  // x=16, outside all zones.
  trace.write(0x447ee, base_a + PLANE_BYTES + 170 * ROW_BYTES + 6,
              0, 0xffff, false, 0xf0, 14);  // x=48, left attitude.
  // Boundary handling: x=224..239 must include the globe's 224..229 bits
  // but exclude the unrelated right-panel 230..239 bits.
  trace.write(0x48000, base_a + 180 * ROW_BYTES + 28,
              0, 0xffff, false, 0xca, 20);

  const auto ranked = trace.ranked();
  assert(trace.site_count() == 4);
  // The left-attitude writer changed all sixteen bits, so it is ranked
  // above the six-bit partial-word globe writer.
  assert(ranked[0].first.pc == 0x447ee);
  assert(ranked[0].first.zone == 0 && ranked[0].second.changed_bits == 16);
  assert(ranked[0].second.min_x == 48 && ranked[0].second.max_x == 63);
  bool found_boundary = false;
  bool found = false;
  for (const auto& [key, site] : ranked) {
    if (key.pc == 0x48000) {
      assert(key.zone == 1 && key.plane == 0);
      assert(!key.line && key.minterm == 0xca);
      assert(site.changed_bits == 6 && site.min_x == 224 && site.max_x == 229);
      found_boundary = true;
    }
    if (key.pc == 0x447ee && key.bank == base_a && key.zone == 1) {
      assert(key.plane == 2 && key.line && key.minterm == 0x3c);
      assert(site.words == 3 && site.changed_words == 2 && site.changed_bits == 2);
      assert(site.min_x == 200 && site.max_x == 200);
      assert(site.first_frame == 10 && site.last_frame == 12);
      found = true;
    }
  }
  assert(found);
  assert(found_boundary);
}
