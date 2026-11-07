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

  // Guest bus writes can cross a zone edge or a 40-byte scanline without
  // falsely attributing pixels outside the observed instrument.
  trace.cpu_write(0x4a100, base_a + 180 * ROW_BYTES + 28, 2, 30);
  trace.cpu_write(0x4a100, base_a + 180 * ROW_BYTES + 28, 2, 31);
  trace.cpu_write(0x4a200, base_b + PLANE_BYTES + 170 * ROW_BYTES + 23,
                  4, 35);
  trace.cpu_write(0x4a300, base_a + PLANE_BYTES + 170 * ROW_BYTES + 2,
                  2, 35);  // Outside every zone.
  trace.cpu_write(0x4a300, base_a + PLANE_BYTES + 170 * ROW_BYTES + 6,
                  0, 35);  // Invalid width.

  const auto cpu = trace.ranked_cpu();
  assert(cpu.size() == 2);
  assert(cpu[0].first.pc == 0x4a200);
  assert(cpu[0].first.bank == base_b && cpu[0].first.plane == 1);
  assert(cpu[0].first.zone == 1 && cpu[0].first.width == 4);
  assert(cpu[0].second.bytes == 4);
  assert(cpu[0].second.min_x == 189 && cpu[0].second.max_x == 215);
  assert(cpu[1].first.pc == 0x4a100 && cpu[1].second.bytes == 2);
  assert(cpu[1].second.min_x == 224 && cpu[1].second.max_x == 229);
  assert(cpu[1].second.first_frame == 30 && cpu[1].second.last_frame == 31);

  // A long store ending at the scanline boundary includes only the
  // status-panel byte at x=312..319, not row 199's unrelated pixels.
  Trace boundary;
  boundary.observe_bank(base_a);
  boundary.cpu_write(0x4b000, base_a + PLANE_BYTES * 3 +
                               198 * ROW_BYTES + 39, 4, 40);
  const auto crossed = boundary.ranked_cpu();
  assert(crossed.size() == 1);
  assert(crossed[0].first.zone == 2 && crossed[0].first.plane == 3);
  assert(crossed[0].second.bytes == 1);
  assert(crossed[0].second.min_x == 312 && crossed[0].second.max_x == 319);
  assert(crossed[0].second.min_y == 198 && crossed[0].second.max_y == 198);

  // A single byte partially overlapping a zone must not expand the bounds
  // to the unobserved pixels at x=32 and x=33.
  boundary.cpu_write(0x4b100, base_a + 193 * ROW_BYTES + 4, 1, 41);
  const auto clipped = boundary.ranked_cpu();
  assert(clipped.size() == 2);
  bool found_clipped = false;
  for (const auto& [key, site] : clipped) {
    if (key.pc != 0x4b100) continue;
    assert(key.zone == 0 && key.plane == 0);
    assert(site.bytes == 1 && site.min_x == 34 && site.max_x == 39);
    found_clipped = true;
  }
  assert(found_clipped);
}
