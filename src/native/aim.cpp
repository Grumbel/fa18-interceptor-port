// Direction vector -> heading/elevation angles with rate-limited smoothing.
// Original: hunk 0 $24552 (compiled C). Called from the assembly hunks
// (HUD target tracker, camera) with six longs pushed by movem.
#include "native/globals.hpp"
#include "native/orig.hpp"
#include "native/vars.hpp"

namespace native {
using namespace g;
using namespace v;

namespace {
constexpr uint32_t atan_table = 0x52E00;   // 339456: word[ ] angle table (1/10 deg units)
inline int32_t table_angle(int16_t i) { return int16_t(rd16(atan_table + uint32_t(int32_t(i)) * 2)); }

// Move `cur` towards `target` on the 0..28800 circle (28800 = 360 degrees
// in 1/80 deg) by at most `limit` (after dividing the difference by 2^shift).
inline int32_t approach(int32_t cur, int32_t target, uint16_t shift, int32_t limit) {
  int32_t delta = target - cur;
  int32_t step;
  if (delta < 0) {
    if (delta >= -14400) {
      step = (-delta) >> shift;
      if (step > limit) step = limit;
      return cur - step;
    }
    step = (delta + 28800) >> shift;      // wrap forwards
    if (step > limit) step = limit;
    int32_t r = step + cur;
    return r >= 28800 ? r - 28800 : r;
  }
  if (delta > 14400) {
    step = (28800 - delta) >> shift;      // wrap backwards
    if (step > limit) step = limit;
    int32_t r = cur - step;
    return r < 0 ? r + 28800 : r;
  }
  step = delta >> shift;
  if (step > limit) step = limit;
  return cur + step;
}
}  // namespace

// $24552: angles of vector (x, y, z) in 1/80 degree units: the angle derived
// from the y/radius ratio ends in word $5adb8 (arg 0 = previous value), the
// angle in the x/z plane in word $5adba (arg 1). With rate >= 0 and the
// tracker running they approach the new values by at most `rate` per call.
static void look_angles(int32_t heading /*y-angle*/, int32_t elevation /*xz-angle*/, int32_t x, int32_t y, int32_t z,
                        int32_t rate) {
  const uint16_t smooth_shift = 2;
  uint8_t sign = 0;
  if (x < 0) { x = -x; sign |= 1; }
  if (y < 0) { y = -y; sign |= 2; }
  if (z < 0) { z = -z; sign |= 4; }
  int32_t max = x > y ? (x > z ? x : z) : (y > z ? y : z);
  int16_t shift, left;
  if (max > 0x10000000) {
    x >>= 3; y >>= 3; z >>= 3;
    shift = 14; left = 0;
  } else if (max > 0x2000000) { shift = 14; left = 0; }
  else if (max > 0x800000) { shift = 12; left = 2; }
  else if (max > 0x200000) { shift = 10; left = 4; }
  else { shift = 8; left = 6; }

  int32_t xs = x >> shift, zs = z >> shift;
  int16_t xw = int16_t(xs), zw = int16_t(zs);
  int32_t h;                            // heading from the x/z plane (fp-18)
  if (xw <= zw) {
    l_5adc4 = x << left;
    w_5adc8 = zw;
    if (zw != 0) {
      sub_39eec();
      w_5adca = int16_t(w_5adca >> 6);
      h = table_angle(int16_t(w_5adca)) * 8;
    } else {
      h = 3600;
    }
  } else {
    l_5adc4 = z << left;
    w_5adc8 = xw;
    sub_39eec();
    w_5adca = int16_t(w_5adca >> 6);
    h = (900 - table_angle(int16_t(w_5adca))) * 8;
  }
  uint8_t q = uint8_t(sign & 5);
  if (q == 5) h = 14400 - h;
  else if (q == 4) h = h + 14400;
  else if (q == 0) h = 28800 - h;

  int32_t zq = int16_t(z >> shift), xq = int16_t(x >> shift);
  l_5ae5c = zq * zq + xq * xq;
  sub_39bba();                          // w_5ae60 = isqrt(...)
  int16_t yw = int16_t(y >> shift);
  int32_t r = int16_t(w_5ae60);
  int32_t e;                            // elevation (fp-14)
  if (yw <= r) {
    l_5adc4 = int32_t(yw) << 8;
    w_5adc8 = int16_t(w_5ae60);
    if (w_5ae60 != 0) {
      sub_39eec();
      e = table_angle(int16_t(w_5adca)) * 8;
    } else {
      e = 3600;
    }
  } else {
    l_5adc4 = r << 8;
    w_5adc8 = yw;
    sub_39eec();
    e = (900 - table_angle(int16_t(w_5adca))) * 8;
  }
  if (!(sign & 2)) e = 28800 - e;
  if (e >= 28800) e = 0;
  if (h >= 28800) h = 0;

  if (rate < 0 || (b_5aba6 | b_5aa9e) == 0) {
    b_5aa9e = 1;
    heading = e;
    elevation = h;
  } else {
    heading = approach(heading, e, smooth_shift, rate);
    elevation = approach(elevation, h, smooth_shift, rate);
  }
  w_5adb8 = int16_t(heading);
  w_5adba = int16_t(elevation);
}
NATIVE_C(0x24552, look_angles, false);

}  // namespace native
