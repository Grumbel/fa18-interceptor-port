// Display set-up (View/ViewPort, double-buffered copper lists, palette
// fade), the text-table loader and the program's main loop.
// Original: hunk 0 $27d50-$28474 and $20fb4/$28670 (compiled C).
#include "native/globals.hpp"
#include "native/orig.hpp"
#include "native/os.hpp"

namespace native {
using namespace g;

namespace {
constexpr uint32_t view_v = 0x2a6e0;          // struct View
constexpr uint32_t vport = 0x2a6f2;           // struct ViewPort
constexpr uint32_t rasinfo = 0x2a71e;         // struct RasInfo
constexpr uint32_t rastport_a = 0x2a79a, rastport_b = 0x2a7fe;
constexpr Var<uint32_t> bitmap_a_ptr{0x2a72a};  // -> bitmap A (5 planes)
constexpr uint32_t bitmap_a = 0x2a732;
constexpr uint32_t bitmap_b_struct = 0x2a75a;   // 4 planes, shares A's planes
constexpr Var<uint32_t> bitmap_b_ptr{0x2a72e};
constexpr Var<uint32_t> view_lof_cprlist{0x2a6e4};
constexpr Var<uint32_t> vport_dspins{0x2a6fa};
constexpr Var<uint32_t> cprlist_save{0x2a782};   // [2] LOFCprList per buffer
constexpr Var<uint32_t> dspins_save{0x2a78a};    // [2] DspIns per buffer
constexpr Var<int16_t> buffer_index{0x5a964};
constexpr Var<uint32_t> fade_target{0x5a958};     // -> 32 colours (chip)
constexpr Var<uint8_t> fade_count{0x5ab91};       // palette flash/fade steps
constexpr Var<uint8_t> fade_request{0x5ab9c};     // single fade-to-target request
constexpr uint32_t flash_palette = 0x3a014;       // 32 colours
constexpr uint32_t str_gfx = 0x2a420;
}  // namespace

// $27f0c: build the View with two double-buffered 320x200 bitmaps (A: 5
// planes, B: the same first 4 planes), the colour map and both copper lists.
static void setup_display() {
  gfx_base = os::OpenLibrary(str_gfx, 29);
  if (gfx_base == 0) sub_6620c(1);
  wr32(0x2a796, rd32(uint32_t(gfx_base) + 34));   // GfxBase->ActiView
  os::InitView(view_v);
  os::InitVPort(vport);
  wr32(view_v, vport);                            // View->ViewPort
  bitmap_a_ptr = bitmap_a;
  bitmap_b_ptr = bitmap_b_struct;
  os::InitBitMap(bitmap_a, 5, 320, 200);
  os::InitBitMap(bitmap_b_struct, 4, 320, 200);
  os::InitRastPort(rastport_a);
  os::InitRastPort(rastport_b);
  wr32(rastport_a + 4, bitmap_a);                 // RastPort->BitMap
  wr32(rastport_b + 4, bitmap_b_struct);
  wr32(rasinfo + 4, bitmap_a);                    // RasInfo->BitMap
  wr16(rasinfo + 8, 0);                           // RxOffset
  wr16(rasinfo + 10, 0);                          // RyOffset
  wr32(rasinfo, 0);                               // Next
  wr16(vport + 0x18, 320);                        // DWidth
  wr16(vport + 0x1a, 200);                        // DHeight
  wr32(vport + 0x24, rasinfo);                    // RasInfo
  uint32_t planes[5];
  for (int i = 0; i < 5; ++i) {
    planes[i] = os::AllocMem(8000, os::MEMF_CHIP | os::MEMF_CLEAR);
    wr32(bitmap_a + 8 + uint32_t(i) * 4, planes[i]);
    if (i < 4) wr32(0x5a9b6 + uint32_t(i) * 4, planes[i]);
    if (i == 3) {
      if (!rd32(0x5a9b6) || !rd32(0x5a9ba) || !rd32(0x5a9be) || !planes[3]) sub_6620c(uint32_t(-1000));
    }
    if (i == 4) {
      wr32(0x5a9c6, planes[4]);
      if (planes[4] == 0) sub_6620c(uint32_t(-1000));
    }
  }
  for (int i = 0; i < 4; ++i) wr32(0x5a9ca + uint32_t(i) * 4, rd32(0x5a9b6 + uint32_t(i) * 4));
  for (int i = 0; i < 4; ++i) wr32(bitmap_b_struct + 8 + uint32_t(i) * 4, rd32(bitmap_a + 8 + uint32_t(i) * 4));
  sub_440e6();
  colormap = os::GetColorMap(32);
  fade_target = os::AllocMem(64, os::MEMF_CHIP | os::MEMF_CLEAR);
  uint32_t table = rd32(uint32_t(colormap) + 4);
  for (int i = 0; i < 32; ++i) wr16(table + uint32_t(i) * 2, palette_buf.at(i));
  wr32(vport + 4, colormap);                      // ViewPort->ColorMap
  wr16(vport + 0x1c, 0);                          // DxOffset
  wr16(vport + 0x1e, uint16_t(-2));               // DyOffset
  os::MakeVPort(view_v, vport);
  os::MrgCop(view_v);
  cprlist_save.at(0) = view_lof_cprlist.get();
  dspins_save.at(0) = vport_dspins.get();
}
NATIVE_C(0x27f0c, setup_display, false);

// $2822e: second buffer: build the copper list for bitmap B.
static void setup_second_buffer() {
  view_lof_cprlist = 0;
  vport_dspins = 0;
  wr32(rasinfo + 4, bitmap_b_ptr);                // RasInfo->BitMap = B
  os::MakeVPort(view_v, vport);
  os::MrgCop(view_v);
  cprlist_save.at(1) = view_lof_cprlist.get();
  dspins_save.at(1) = vport_dspins.get();
  buffer_index = 0;
}
NATIVE_C(0x2822e, setup_second_buffer, false);

// $28284: show the finished frame: wait for the display to pass the bottom
// of the viewport, switch to the other copper list and run palette
// flashes/fades requested by the game.
static void swap_buffers() {
  os::WaitBOVP(vport);
  int idx = buffer_index;
  view_lof_cprlist = rd32(0x2a782 + uint32_t(idx) * 4);
  vport_dspins = rd32(0x2a78a + uint32_t(idx) * 4);
  os::LoadView(view_v);
  if (fade_count != 0) {
    os::WaitBOVP(vport);
    os::WaitBlit();
    while (int8_t(fade_count) > 0) {
      os::LoadRGB4(vport, flash_palette, 32);
      os::WaitBOVP(vport);
      os::WaitBOVP(vport);
      os::LoadRGB4(vport, fade_target, 32);
      os::WaitBOVP(vport);
      os::WaitBOVP(vport);
      fade_count = uint8_t(fade_count - 1);
    }
  } else if (fade_request != 0) {
    fade_request = uint8_t(fade_request - 1);
    if (!(rd16(0x5abca) & 0x100)) {
      os::WaitBOVP(vport);
      os::LoadRGB4(vport, fade_target, 32);
    }
  }
  buffer_index = int16_t(1 - buffer_index);
}
NATIVE_C(0x28284, swap_buffers, false);

// $283dc: release all eight hardware sprites.
static void free_all_sprites() {
  for (int i = 0; i < 8; ++i) os::FreeSprite(i);
}
NATIVE_C(0x283dc, free_all_sprites, false);

// $20fb4: allocate the text tables (half the public memory, at most
// 125000 bytes); returns 1 on success.
static int32_t alloc_text_buffers() {
  int32_t ok = 0;
  int32_t avail = int32_t(os::AvailMem(0x10001));
  int32_t v = avail - (avail >> 1);
  if (v > 125000) v = 125000;
  v >>= 4;
  uint32_t buf = os::AllocMem(uint32_t(v + 2), 0x10001);
  wr32(0x65094, buf);
  if (buf != 0) {
    wr32(0x65098, uint32_t(v));
    uint32_t tbl = os::AllocMem(uint32_t(v * 4 + 8), 0x10001);
    wr32(0x650a0, tbl);
    if (tbl != 0) ok = 1;
  }
  if (ok == 0) {
    wr32(0x65094, 0);
    wr32(0x650a0, 0);
  }
  return int32_t(int16_t(ok));
}
NATIVE_C(0x20fb4, alloc_text_buffers, false);

// $28670: read df0:text/textply and df0:text/textctl into the text tables
// (creating the files if they do not exist).
static void load_text_tables() {
  int32_t fh = os::Open(0x2a457, os::MODE_OLDFILE);
  if (fh > 0) {
    os::Read(fh, rd32(0x65094), int32_t(rd32(0x65098)));
    wr32(0x6509c, rd32(0x65094));
  } else {
    fh = os::Open(0x2a468, os::MODE_NEWFILE);
  }
  os::Close(fh);
  fh = os::Open(0x2a479, os::MODE_OLDFILE);
  if (fh > 0) {
    os::Read(fh, rd32(0x650a0), int32_t(rd32(0x65098)) << 2);
  } else {
    fh = os::Open(0x2a48a, os::MODE_NEWFILE);
  }
  os::Close(fh);
}
NATIVE_C(0x28670, load_text_tables, false);

[[noreturn]] static void main_loop() {
  mach().in_main_loop = true;
  for (;;) {
    mach().apply_state_requests();   // save/load states (remaster/debug menu)
    sub_44160();
    os::OwnBlitter();
    sub_2112c();                                       // game_frame
    os::DisownBlitter();
    swap_buffers();
  }
}

// $27d50: start of the game proper: picture planes, buffers, devices, the
// fade-in of the title colours, then the endless main loop.
static void game_main() {
  sub_28a38();                                        // free 5th planes
  sub_28ada();                                        // cockpit planes/mask
  free_all_sprites();
  wr32(0x5a950, os::AvailMem(2));
  wr32(0x650bc, os::AllocMem(7000, 1));
  sub_21630();                                        // init_game
  wr32(0x2d03c, os::AllocMem(78, 0x10001));           // flight log record
  wr32(0x5a954, os::AvailMem(2));
  if (alloc_text_buffers() != 0) load_text_tables();
  os::FreeSprite(0);
  if (rd8(0x5ae53) & 0x80) sub_208e2(0x80000);
  for (int16_t step = 0; step < 16; ++step) {
    uint32_t table = rd32(uint32_t(colormap) + 4);
    for (int16_t i = 0; i < 32; ++i) {
      uint16_t c = rd16(table + uint32_t(i) * 2);
      int16_t r = int16_t((c & 0xF00) - 0x100); if (r < 0) r = 0;
      int16_t gr = int16_t((c & 0xF0) - 0x10); if (gr < 0) gr = 0;
      int16_t b = int16_t((c & 0xF) - 1); if (b < 0) b = 0;
      wr16(table + uint32_t(i) * 2, uint32_t(uint16_t(r | gr | b)));
    }
    os::WaitBOVP(vport);
    os::LoadRGB4(vport, rd32(uint32_t(colormap) + 4), 32);
    sub_208e2(2560);
  }
  setup_second_buffer();
  if (rd32(0x650bc) != 0) os::FreeMem(rd32(0x650bc), 7000);
  sub_208e2(100000);
  main_loop();
}
NATIVE_C(0x27d50, game_main, false);

[[noreturn]] void resume_main_loop() { main_loop(); }

}  // namespace native
