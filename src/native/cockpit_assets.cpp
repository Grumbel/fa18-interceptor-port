// Original ILBM/ByteRun1 cockpit decoder. Keep the *indices* rather than the
// ILBM RGB palette: the 5-bit Amiga copper palette can change on each row.
// The original assets stay on the user's ADF, never in the repository.
#include "native/cockpit_assets.hpp"
#include "amiga/machine.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace remaster {
namespace {

struct Ilbm {
  int w = 0, h = 0;
  std::vector<uint8_t> index;
  std::vector<uint8_t> visible;
  std::vector<uint32_t> rgb;
};

uint16_t be16(const uint8_t* p) { return uint16_t((uint16_t(p[0]) << 8) | p[1]); }
uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

bool decode_ilbm(const std::vector<uint8_t>& bytes, int wanted_w, int wanted_h, Ilbm& out) {
  if (bytes.size() < 12 || std::memcmp(bytes.data(), "FORM", 4) != 0 ||
      std::memcmp(bytes.data() + 8, "ILBM", 4) != 0) return false;
  const size_t form_end = std::min(bytes.size(), size_t(8) + be32(bytes.data() + 4));
  const uint8_t* bmhd = nullptr;
  const uint8_t* body = nullptr;
  size_t body_size = 0;
  const uint8_t* cmap = nullptr;
  size_t cmap_size = 0;
  for (size_t off = 12; off + 8 <= form_end;) {
    const uint8_t* chunk = bytes.data() + off;
    const size_t size = be32(chunk + 4);
    off += 8;
    if (size > form_end - off) return false;
    if (std::memcmp(chunk, "BMHD", 4) == 0) {
      if (size < 20) return false;
      bmhd = bytes.data() + off;
    } else if (std::memcmp(chunk, "BODY", 4) == 0) {
      body = bytes.data() + off;
      body_size = size;
    } else if (std::memcmp(chunk, "CMAP", 4) == 0) {
      cmap = bytes.data() + off;
      cmap_size = size;
    }
    off += size + (size & 1);
  }
  if (!bmhd || !body || !cmap || cmap_size < 3 ||
      be16(bmhd) != wanted_w || be16(bmhd + 2) != wanted_h ||
      bmhd[8] != 5 || (bmhd[9] != 0 && bmhd[9] != 2) || bmhd[10] > 1) return false;
  const bool transparent = bmhd[9] == 2;
  const uint8_t transparent_index = uint8_t(be16(bmhd + 12) & 31);
  const int rowbytes = ((wanted_w + 15) / 16) * 2;
  const size_t expected = size_t(rowbytes) * 5 * wanted_h;
  std::vector<uint8_t> planar;
  planar.reserve(expected);
  if (bmhd[10] == 0) {
    if (body_size != expected) return false;
    planar.assign(body, body + body_size);
  } else {
    const uint8_t* end = body + body_size;
    const uint8_t* p = body;
    while (planar.size() < expected) {
      if (p == end) return false;
      const int8_t run = int8_t(*p++);
      if (run >= 0) {
        const size_t count = size_t(run) + 1;
        if (size_t(end - p) < count || count > expected - planar.size()) return false;
        planar.insert(planar.end(), p, p + count);
        p += count;
      } else if (run != -128) {
        const size_t count = size_t(1 - int(run));
        if (p == end || count > expected - planar.size()) return false;
        planar.insert(planar.end(), count, *p++);
      }
    }
  }
  out.w = wanted_w;
  out.h = wanted_h;
  out.index.resize(size_t(wanted_w) * wanted_h);
  out.visible.resize(out.index.size());
  out.rgb.resize(out.index.size());
  for (int y = 0; y < wanted_h; ++y)
    for (int x = 0; x < wanted_w; ++x) {
      uint8_t index = 0;
      for (int plane = 0; plane < 5; ++plane) {
        const size_t off = (size_t(y) * 5 + plane) * rowbytes + x / 8;
        index |= uint8_t(((planar[off] >> (7 - (x & 7))) & 1) << plane);
      }
      const size_t at = size_t(y) * wanted_w + x;
      out.index[at] = index;
      out.visible[at] = !(transparent && index == transparent_index);
      // The ILBM CMAP stores 4-bit RGB components as 0x00,0x10,...0xf0;
      // the live Amiga 12-bit DAC expands them to 0x00,0x11,...0xff.
      const size_t ci = size_t(index) * 3;
      const uint8_t r = ci + 2 < cmap_size ? uint8_t((cmap[ci] >> 4) * 17) : 0;
      const uint8_t g = ci + 2 < cmap_size ? uint8_t((cmap[ci + 1] >> 4) * 17) : 0;
      const uint8_t b = ci + 2 < cmap_size ? uint8_t((cmap[ci + 2] >> 4) * 17) : 0;
      out.rgb[at] = 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
    }
  return true;
}

CockpitAtlas atlas;
bool attempted = false;

void place_asset(const amiga::Machine& machine, const char* name,
                 int w, int h, int x0, int y0) {
  Ilbm image;
  if (!decode_ilbm(machine.disk_read(name), w, h, image)) {
    if (std::getenv("FA18_COCKPIT_DEBUG"))
      std::fprintf(stderr, "cockpit: could not decode %s from ADF\n", name);
    return;
  }
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const size_t src = size_t(y) * w + x;
      if (!image.visible[src]) continue;
      const size_t dst = size_t(y0 + y) * CockpitAtlas::W + x0 + x;
      atlas.index[dst] = image.index[src];
      atlas.rgb[dst] = image.rgb[src];
      atlas.valid[dst] = 1;
    }
  if (std::getenv("FA18_COCKPIT_DEBUG"))
    std::fprintf(stderr, "cockpit: decoded %s %dx%d at %d,%d\n", name, w, h, x0, y0);
}

}  // namespace

const CockpitAtlas& cockpit_atlas(const amiga::Machine& machine) {
  if (attempted) return atlas;
  attempted = true;
  // The exact positions were established by comparison against an in-flight
  // 320x200 screenshot. At VBL 4000, inst5 matches 17,332/17,600 pixels
  // after converting 4-bit RGB components to the live Amiga palette.
  place_asset(machine, "pix/inst5", 320, 55, 0, 145);
  place_asset(machine, "pix/frnt5", 288, 12, 16, 133);
  atlas.rgba.assign(size_t(CockpitAtlas::W) * CockpitAtlas::H * 4, 0);
  for (size_t i = 0; i < atlas.index.size(); ++i) {
    uint8_t* p = &atlas.rgba[i * 4];
    p[0] = uint8_t(atlas.rgb[i] >> 16);
    p[1] = uint8_t(atlas.rgb[i] >> 8);
    p[2] = uint8_t(atlas.rgb[i]);
    p[3] = atlas.index[i];
    atlas.opaque += atlas.valid[i];
  }
  if (std::getenv("FA18_COCKPIT_DEBUG"))
    std::fprintf(stderr, "cockpit: %d native static artwork pixels available\n", atlas.opaque);
  return atlas;
}

void reset_cockpit_atlas() {
  atlas = CockpitAtlas{};
  attempted = false;
}

}  // namespace remaster
