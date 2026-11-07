#include "native/gl_scene_batch_cache.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <optional>

int main() {
  using glr::SceneBatchCacheKey;
  using glr::move_batch_positions;

  std::optional<SceneBatchCacheKey> uploaded;
  const SceneBatchCacheKey first{42, 1.f, false};
  assert(uploaded != first);
  uploaded = first;
  assert(uploaded == first);
  assert((uploaded != SceneBatchCacheKey{43, 1.f, false}));
  assert((uploaded != SceneBatchCacheKey{42, .5f, true}));
  assert((uploaded != SceneBatchCacheKey{42, 1.f, true}));

  // New interpolation positions can be reordered in the packed draw list.
  // Palette indices and depth bias must remain bit-exact across every blend.
  const std::vector<float> initial{10, 11, 12, 20, 21, 22, 30, 31, 32};
  const std::vector<float> halfway{15, 16, 17, 25, 26, 27, 35, 36, 37};
  std::vector<float> styled{30, 31, 32, .125f, .00002f,
                             10, 11, 12, .875f, .00004f,
                             20, 21, 22, .625f, .00006f};
  const std::vector<size_t> order{6, 0, 3};
  move_batch_positions(styled, order, halfway, 5);
  assert((styled == std::vector<float>{35, 36, 37, .125f, .00002f,
                                       15, 16, 17, .875f, .00004f,
                                       25, 26, 27, .625f, .00006f}));
  move_batch_positions(styled, order, initial, 5);
  assert((styled == std::vector<float>{30, 31, 32, .125f, .00002f,
                                       10, 11, 12, .875f, .00004f,
                                       20, 21, 22, .625f, .00006f}));

  std::vector<float> plain{10, 11, 12, 30, 31, 32};
  move_batch_positions(plain, {0, 6}, halfway, 3);
  assert((plain == std::vector<float>{15, 16, 17, 35, 36, 37}));
  std::vector<float> empty;
  move_batch_positions(empty, {}, halfway, 5);
}
