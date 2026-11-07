#include "native/gl_scene_rows.hpp"

#include <cassert>
#include <cstdint>

int main() {
  using remaster::gl_scene_rows::select;
  using remaster::gl_scene_rows::last_palette_row;
  for (int rows : {1, 145, 168, 180, 200}) {
    assert(last_palette_row(rows) == rows - 1);
    for (int y = 0; y < 200; ++y) {
      // Normal 3D source buffer: Amiga's scanline pointer can refer to an
      // earlier completed layer, so the source 3D row is not always y.
      const int old_y = y < rows ? (y + 3) % rows : -1;
      const auto matched = select(y, rows, old_y, false, 0x10, 0x00);
      assert(matched.scene_y == (old_y < 0 ? y : old_y));
      assert(matched.has_scene_row == (old_y >= 0));
      assert(matched.side_scene == (old_y >= 0));
      assert(matched.side_palette_y == (y < rows ? y : rows - 1));
      assert(matched.left_ehb == (old_y >= 0));
      assert(!matched.right_ehb);

      // In an unfinished transition bank only the prior completed scene
      // supplies the widescreen extension. Its EHB bits must not leak from
      // unrelated source pixels. The CPU and GPU paths use this one map.
      const auto missing = select(y, rows, -1, true, 0x10, 0x10);
      assert(missing.side_scene && !missing.has_scene_row);
      assert(missing.scene_y == y);
      assert(!missing.left_ehb && !missing.right_ehb);
      assert(missing.side_palette_y == (y < rows ? y : rows - 1));

      const auto absent = select(y, rows, -1, false, 0x10, 0x10);
      assert(!absent.side_scene && !absent.has_scene_row);
      assert(absent.scene_y == y);
      assert(!absent.left_ehb && !absent.right_ehb);
    }
  }

  // A genuine matched scene row stays visible even when the flight flag is
  // temporarily cleared. This previously differed between CPU and GPU.
  const auto stale = select(51, 145, 47, false, 0x10, 0x10);
  assert(stale.side_scene && stale.scene_y == 47);
  assert(stale.left_ehb && stale.right_ehb);

  // The cockpit copper palette itself remains live below the 3D view.
  // Only the outboard scenery uses this clamped palette row index.
  const auto nose = select(175, 145, -1, true, 0, 0);
  assert(nose.side_palette_y == 144);
  assert(nose.side_palette_y != 175);
  assert(last_palette_row(0) == 0);
  assert(last_palette_row(300) == 199);
}
