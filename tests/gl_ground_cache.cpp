#include "native/gl_ground_cache.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <optional>

int main() {
  using glr::GroundCacheKey;
  const GroundCacheKey base{1234.0, -5678.0, 200.0, 25, false};
  assert(base == base);
  std::optional<GroundCacheKey> uploaded;
  assert(uploaded != base);  // the first presentation always uploads
  uploaded = base;
  assert(uploaded == base);
  assert((uploaded != GroundCacheKey{1235.0, -5678.0, 200.0, 25, false}));
  assert((uploaded != GroundCacheKey{1234.0, -5679.0, 200.0, 25, false}));
  assert((uploaded != GroundCacheKey{1234.0, -5678.0, 201.0, 25, false}));
  assert((uploaded != GroundCacheKey{1234.0, -5678.0, 200.0, 26, false}));
  assert((uploaded != GroundCacheKey{1234.0, -5678.0, 200.0, 25, true}));
  // View rotation, framebuffer width, render scale and interpolation alpha
  // do not appear in the key: their shader transforms cannot alter the
  // camera-relative vertex stream.
}
