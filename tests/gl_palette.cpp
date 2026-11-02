#include "native/gl_palette.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>

int main() {
  for (int index = 0; index < 16; ++index) {
    const float red = glr::palette::encode(uint8_t(index));
    // Simulate GL renderbuffer quantization and byte-valued glReadPixels.
    const int r8 = int(std::lround(red * 255.f));
    const int r4 = int(std::lround(red * 15.f)) * 17;
    assert(glr::palette::decode(uint8_t(r8)) == index);
    assert(glr::palette::decode(uint8_t(r4)) == index);
  }
}
