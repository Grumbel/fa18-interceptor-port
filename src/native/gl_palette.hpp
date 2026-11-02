// Store Amiga palette indices (0..15) losslessly in either RGBA8 or RGBA4.
// GLES2 may only support RGBA4 renderbuffers.  Encoding as index/255 and
// masking the readback byte's low nibble works for RGBA8, but destroys all
// but two colours in RGBA4 because that channel has only four bits.
#pragma once

#include <cstdint>

namespace glr::palette {

inline float encode(uint8_t index) { return float(index & 15) / 15.f; }
inline uint8_t decode(uint8_t red) { return red >> 4; }

}  // namespace glr::palette
