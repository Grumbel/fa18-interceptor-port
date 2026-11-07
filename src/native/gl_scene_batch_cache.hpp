// Key for the immutable, already-styled GL primitive streams of one capture.
// Only the capture and its interpolated vertex positions affect the streams;
// view rotation, framebuffer dimensions and scanline palettes are uniforms.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace glr {

struct SceneBatchCacheKey {
  uint64_t serial = 0;
  float alpha = 1.f;
  bool interpolated = false;

  bool operator==(const SceneBatchCacheKey&) const = default;
};

// Reuse the source-to-packed-vertex map when only the interpolation phase
// changes. The remaining two floats in a styled vertex (palette/depth bias)
// are immutable within one capture and must never be recomputed or rounded.
inline void move_batch_positions(std::vector<float>& packed,
                                 const std::vector<size_t>& source_offsets,
                                 const std::vector<float>& positions,
                                 size_t stride) {
  assert(stride >= 3 && packed.size() == source_offsets.size() * stride);
  for (size_t i = 0; i < source_offsets.size(); ++i) {
    const size_t at = source_offsets[i];
    assert(at + 3 <= positions.size());
    for (size_t axis = 0; axis < 3; ++axis)
      packed[i * stride + axis] = positions[at + axis];
  }
}

}  // namespace glr
