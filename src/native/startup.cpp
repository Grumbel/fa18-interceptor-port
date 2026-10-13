// Program start: main(), picture loading and start-up helpers.
// Original: hunk 0 $201d0-$20fb4 (compiled C; the picture loader used EA's
// public IFF-85 reader, replaced here by a direct ILBM decoder that produces
// the same memory layout).
#include "native/globals.hpp"
#include "native/orig.hpp"
#include "native/os.hpp"

#include <vector>

namespace native {
using namespace g;

// ---- ILBM loading ------------------------------------------------------------

namespace {
uint32_t be32(const std::vector<uint8_t>& b, size_t o) {
  return o + 4 <= b.size() ? uint32_t(b[o]) << 24 | uint32_t(b[o + 1]) << 16 | uint32_t(b[o + 2]) << 8 | b[o + 3] : 0;
}
}  // namespace

// $201d0: load an ILBM picture. Fills the 20-byte BitMapHeader at `bmhd`
// and up to 32 palette entries (12-bit) at `palette`; returns a new BitMap
// (planes allocated in chip memory) or 0.
//
// Like the original this reads the chunks in the fixed order the game's
// pictures use: FORM ILBM, BMHD, CMAP, CAMG, BODY. The palette size is taken
// from the FORM length / 3, capped at 32 (all CMAPs have 32 entries).
uint32_t load_picture(uint32_t name, uint32_t palette, uint32_t bmhd) {
  int32_t fh = os::Open(name, os::MODE_OLDFILE);
  if (fh == 0) return 0;
  int32_t len = os::Seek(fh, 0, 1);  // OFFSET_END; returns old position
  len = os::Seek(fh, 0, -1);         // back to start; returns file length
  if (len < 8) return 0;
  std::vector<uint8_t> file(static_cast<size_t>(len));
  {
    uint32_t tmp = os::AllocMem(uint32_t(len), 0);
    os::Read(fh, tmp, len);
    for (int32_t i = 0; i < len; ++i) file[size_t(i)] = rd8(tmp + uint32_t(i));
    os::FreeMem(tmp, uint32_t(len));
  }
  uint32_t form_size = be32(file, 4);
  size_t pos = 12;  // after "FORM" <size> "ILBM"
  pos += 8;         // BMHD chunk header
  for (int i = 0; i < 20; ++i) wr8(bmhd + uint32_t(i), file[pos + size_t(i)]);
  pos += 20 + 8;    // BMHD data, CMAP chunk header
  uint32_t ncolors = form_size / 3;
  if (ncolors > 32) ncolors = 32;
  for (uint32_t i = 0; i < ncolors; ++i, pos += 3) {
    uint8_t r = file[pos], gr = file[pos + 1], b = file[pos + 2];
    wr16(palette + i * 2, uint32_t((r >> 4) << 8 | (gr >> 4) << 4 | (b >> 4)));
  }
  pos += 8 + 4 + 8;  // CAMG header + data, BODY header

  uint16_t width = rd16(bmhd + 0), height = rd16(bmhd + 2);
  uint8_t depth = rd8(bmhd + 8), masking = rd8(bmhd + 9), compression = rd8(bmhd + 10);
  uint32_t bm = os::AllocMem(40, os::MEMF_CHIP | os::MEMF_CLEAR);
  os::InitBitMap(bm, depth, width, height);
  for (uint32_t p = 0; p < depth; ++p)
    wr32(bm + 8 + p * 4, os::AllocMem((uint32_t(width) * height) >> 3, os::MEMF_CHIP | os::MEMF_CLEAR));
  uint16_t bpr = rd16(bm);
  // Decompression buffer of the original (never freed).
  uint32_t bufsize = ((uint32_t(bpr) + 0x7F) >> 7) + bpr;
  os::AllocMem(bufsize * 2, os::MEMF_CHIP | os::MEMF_CLEAR);

  // BODY: rows of interleaved planes, ByteRun1 or raw.
  uint16_t rowbytes = uint16_t(((width + 15) >> 4) * 2);
  if (rowbytes == bpr && compression <= 1 && depth < 18) {
    uint32_t rows = std::min<uint32_t>(rd16(bm + 2), height);
    uint32_t dst[17];
    int nplanes = depth + (masking == 1 ? 1 : 0);
    for (int p = 0; p < nplanes; ++p) dst[p] = p < rd8(bm + 5) ? rd32(bm + 8 + uint32_t(p) * 4) : 0;
    auto in = [&]() -> uint8_t { return pos < file.size() ? file[pos++] : 0; };
    for (uint32_t y = 0; y < rows; ++y) {
      for (int p = 0; p < nplanes; ++p) {
        uint32_t out = dst[p];
        if (compression == 0) {
          for (int i = 0; i < rowbytes; ++i) { uint8_t v = in(); if (out) wr8(out + uint32_t(i), v); }
        } else {
          int i = 0;
          while (i < rowbytes) {
            int8_t n = int8_t(in());
            if (n >= 0) {
              for (int k = 0; k <= n && i < rowbytes; ++k, ++i) { uint8_t v = in(); if (out) wr8(out + uint32_t(i), v); }
            } else if (n != -128) {
              uint8_t v = in();
              for (int k = 0; k <= -n && i < rowbytes; ++k, ++i) if (out) wr8(out + uint32_t(i), v);
            }
          }
        }
        if (dst[p]) dst[p] += rowbytes;
      }
    }
  }
  os::Close(fh);
  return bm;
}
NATIVE_C(0x201d0, load_picture, false);

// ---- main ---------------------------------------------------------------------

// $208e2: busy-wait delay used for timed screens (n loop iterations of
// ~76 clocks on a 7 MHz 68000), then FreeSprite(0).
static void delay(int32_t n) {
  mach().idle_lines(int(uint64_t(n < 0 ? 0 : n) * 76 / amiga::CYCLES_PER_LINE));
  os::FreeSprite(0);
}
NATIVE_C(0x208e2, delay, false);

// $207b6: close the Workbench screen to free its memory.
static void close_workbench() {
  intuition_base = os::OpenLibrary(str_intuition, 29);
  if (intuition_base == 0) sub_6620c(0);  // exit(0)
  os::FreeSprite(0);
  os::CloseWorkBench();
  os::FreeSprite(0);
  if (intuition_base != 0) os::CloseLibrary(intuition_base);
}
NATIVE_C(0x207b6, close_workbench, false);

// $2080e: after the title: release the splash picture's 5th plane and give
// planes 0-3 to the game's second (4-plane) BitMap, allocate work buffers.
static void reuse_splash_planes() {
  uint32_t splash = splash_bitmap;
  os::FreeRaster(rd32(splash + 0x18), splash_bmhd_w, splash_bmhd_h);
  uint32_t bm = bitmap_b;
  for (int p = 0; p < 4; ++p) {
    uint32_t plane = rd32(splash + 8 + uint32_t(p) * 4);
    game_planes.at(p) = plane;
    wr32(bm + 8 + uint32_t(p) * 4, plane);
  }
  sub_440e6();
  work_buffer = os::AllocMem(8000, os::MEMF_CHIP | os::MEMF_CLEAR);
  if (work_buffer == 0) sub_6620c(uint32_t(-1000));
  misc_buffer_6510c = os::AllocMem(0x202, 0x10001);
}
NATIVE_C(0x2080e, reuse_splash_planes, false);

// $203d6: main(). Title/credits sequence, then hand over to the game loop
// set up by $27d50.
static void main_() {
  fade_steps = 3;
  gfx_base = os::OpenLibrary(str_graphics, 29);
  if (gfx_base == 0) sub_6620c(0);
  if (sub_6556c() != 0) sub_65648();   // audio.device / sound driver
  title_flag_5ab47 = 0;
  sound_flags = 0;
  title_flag_5ab46 = 1;
  splash_bitmap = load_picture(str_splsh, palette_buf, splash_bmhd);
  os::FreeSprite(0);
  sub_29668();
  os::FreeSprite(0);
  if (int32_t(os::AvailMem(os::MEMF_CHIP)) > 200000) sub_296c2();
  os::FreeSprite(0);
  sub_27f0c();         // display setup
  close_workbench();
  os::WaitBOVP(viewport);
  os::LoadView(view);
  uint32_t splash = splash_bitmap;
  splash_checksum = int16_t(sub_3931c(rd32(splash + 8), rd32(splash + 0xc), rd32(splash + 0x10),
                                      rd32(splash + 0x14), splash_bmhd_w, splash_bmhd_h));
  if (splash_checksum == -0x3aa0) {
    os::WaitBOVP(viewport);
    delay(0x21000);
    os::FreeSprite(0);
    music_volume = 0x3f0000;
    music_volume2 = 0x3f0000;
    if ((sound_flags & 0x80) == 0) sub_29d82(0xf);
    else sub_29cee(0x32);
    delay(0x43000);
    sub_28a8a();
  }
  // Black palette for the credits.
  uint32_t colors = rd32(colormap + 4);
  for (int i = 0; i < 32; ++i) wr16(colors + uint32_t(i) * 2, 0);
  os::WaitBOVP(viewport);
  os::LoadRGB4(viewport, rd32(colormap + 4), 32);
  if (splash_checksum == -0x3aa0) {
    // Title picture: blit it in, fade the palette up, show it.
    os::OwnBlitter();
    sub_44160();
    os::WaitBlit();
    sub_452d8();
    os::WaitBlit();
    os::DisownBlitter();
    uint32_t cols = rd32(colormap + 4);
    for (int i = 0; i < 32; ++i) wr16(cols + uint32_t(i) * 2, palette_buf.at(i));
    os::WaitBOVP(viewport);
    os::LoadRGB4(viewport, rd32(colormap + 4), 32);
    if (sound_flags & 0x80) {
      delay(0x20000);
    } else {
      sub_29d82(50);
      delay(0xa000);
    }
  }
  reuse_splash_planes();
  os::FreeSprite(0);
  if (gfx_base != 0) os::CloseLibrary(gfx_base);
  cockpit_bitmap = load_picture(str_inst5, palette_buf, cockpit_bmhd);
  front_bitmap = load_picture(str_frnt5, palette_buf, front_bmhd);
  uint32_t ck = cockpit_bitmap;
  cockpit_checksum = int16_t(sub_3931c(rd32(ck + 8), rd32(ck + 0xc), rd32(ck + 0x10), rd32(ck + 0x14),
                                       cockpit_bmhd_w, cockpit_bmhd_h));
  uint32_t fr = front_bitmap;
  front_checksum = int16_t(sub_3931c(rd32(fr + 8), rd32(fr + 0xc), rd32(fr + 0x10), rd32(fr + 0x14),
                                     front_bmhd_w, front_bmhd_h));
  sub_27d50();   // game main loop
}
NATIVE_C(0x203d6, main_, false);

}  // namespace native
