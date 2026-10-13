// Device set-up (keyboard.device, timer.device, gameport.device), mouse
// accumulator server install, and picture-plane helpers.
// Original: hunk 0 $28a38-$29284 and $295ae/$295e4 (compiled C).
#include "native/globals.hpp"
#include "native/orig.hpp"
#include "native/os.hpp"

namespace native {
using namespace g;

namespace {
// keyboard.device
constexpr Var<uint32_t> kbd_io{0x2a558};         // IOStdReq*
constexpr Var<uint32_t> kbd_port{0x2a580};       // MsgPort*
constexpr uint32_t str_keyboard_device = 0x2a584;
constexpr Var<int16_t> kbd_open_result{0x2d078};
constexpr Var<uint32_t> kbd_event_ptr{0x2d074};
constexpr uint32_t kbd_event_buf = 0x2d07a;       // 22 bytes (InputEvent)
// timer.device
constexpr uint32_t str_timer_device = 0x2a5ac;
constexpr uint32_t timer_io = 0x2d04c;            // struct timerequest
// gameport.device
constexpr uint32_t str_gameport_device = 0x2a5be;
constexpr Var<uint32_t> gp_scratch_ptr{0x2d0b0};  // -> gp_scratch (1 byte)
constexpr uint32_t gp_scratch = 0x2d09a;
constexpr Var<uint32_t> gp_data_ptr{0x2d092};
constexpr Var<uint32_t> gp_port{0x2d0b4};
constexpr Var<uint32_t> gp_io{0x2d096};
// mouse
constexpr Var<uint16_t> mouse_limit_x0{0x2a5d0}, mouse_limit_y0{0x2a5d2},
    mouse_limit_x1{0x2a5d4}, mouse_limit_y1{0x2a5d6};
constexpr Var<uint16_t> mouse_last_x{0x2d0ce}, mouse_last_y{0x2d0d0};
constexpr uint32_t mouse_server = 0x2d0b8;        // struct Interrupt
constexpr uint32_t mouse_server_code = 0x292e6;
}  // namespace

// $28ce4: open keyboard.device and start the first KBD_READEVENT.
static void open_keyboard() {
  kbd_open_result = int16_t(os::OpenDevice(str_keyboard_device, 0, kbd_io, 0));
  sub_295f8(uint32_t(kbd_port));  // MakePort
  uint32_t io = kbd_io;
  wr32(io + 14, kbd_port);   // mn_ReplyPort
  wr32(io + 36, 22);         // io_Length
  wr32(io + 40, kbd_event_buf);
  wr16(io + 28, 9);          // KBD_READEVENT
  kbd_event_ptr = kbd_event_buf;
  os::SendIO(io);
}
NATIVE_C(0x28ce4, open_keyboard, false);

// $28e30: open timer.device (unit 0).
static void open_timer() {
  int16_t err = int16_t(os::OpenDevice(str_timer_device, 0, timer_io, 0));
  (void)err;  // result unused (the original tests it and does nothing)
}
NATIVE_C(0x28e30, open_timer, false);

// $2914c: gameport.device SetCType / controller type (command 11).
static int32_t gameport_set_ctype(int16_t type) {
  uint32_t io = gp_io;
  wr16(io + 28, 11);               // GPD_SETCTYPE
  wr32(io + 36, 1);
  wr32(io + 40, gp_scratch_ptr);
  wr8(gp_scratch_ptr, uint32_t(type));
  os::SendIO(io);
  os::WaitPort(gp_port);
  os::GetMsg(gp_port);
  return int32_t(int8_t(rd8(io + 31)));  // io_Error
}
NATIVE_C(0x2914c, gameport_set_ctype, false);

// $291be: gameport.device SetTrigger (command 13): report all key-down and
// key-up transitions, timeout 1, 1x1.
static int32_t gameport_set_trigger() {
  uint32_t io = gp_io;
  // The GamePortTrigger lives in this routine's own stack frame (a6-8);
  // it is reached through the dispatcher so the frame is the original's.
  uint32_t trig = cpu().a[6] - 8;
  wr16(io + 28, 13);               // GPD_SETTRIGGER
  wr32(io + 36, 8);
  wr32(io + 40, trig);
  wr16(trig + 0, 3);               // gpt_Keys
  wr16(trig + 2, 0);               // gpt_Timeout
  wr16(trig + 4, 1);               // gpt_XDelta
  wr16(trig + 6, 1);               // gpt_YDelta
  return os::DoIO(io);
}
NATIVE_C(0x291be, gameport_set_trigger, false);

// $28ea4: create the port/request, open gameport.device unit 1 and start
// the first GPD_READEVENT. exit(-n) on failure like the original.
static void open_gameport() {
  gp_scratch_ptr = gp_scratch;
  gp_port = os::CreatePort(0, 0);
  if (gp_port == 0) sub_6620c(uint32_t(-1));
  gp_io = sub_6937c(uint32_t(gp_port));   // CreateStdIO
  if (gp_io == 0) {
    os::DeletePort(gp_port);
    sub_6620c(uint32_t(-2));
  }
  if (os::OpenDevice(str_gameport_device, 1, gp_io, 0) != 0) {
    sub_69394(uint32_t(gp_io));          // DeleteStdIO
    os::DeletePort(gp_port);
    sub_6620c(uint32_t(-3));
  }
  gp_data_ptr = gp_scratch;
  if (int32_t(sub_2914c(3)) != 0) {
    sub_69394(uint32_t(gp_io));
    os::DeletePort(gp_port);
    sub_6620c(uint32_t(-4));
  }
  if (int32_t(sub_291be()) != 0) {
    sub_69394(uint32_t(gp_io));
    os::DeletePort(gp_port);
    sub_6620c(uint32_t(-4));
  }
  uint32_t io = gp_io;
  wr16(io + 28, 9);                // GPD_READEVENT
  wr32(io + 40, gp_scratch);
  wr32(io + 36, 22);
  wr8(io + 30, 0);                 // io_Flags
  os::SendIO(io);
}
NATIVE_C(0x28ea4, open_gameport, false);

// $2925c: mouse clamp rectangle (arguments are 32-bit slots, low words used).
static void set_mouse_limits(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
  mouse_limit_x0 = uint16_t(x0);
  mouse_limit_y0 = uint16_t(y0);
  mouse_limit_x1 = uint16_t(x1);
  mouse_limit_y1 = uint16_t(y1);
}
NATIVE_C(0x2925c, set_mouse_limits, true);

// $295ae: install the vertical-blank server that accumulates JOY0DAT.
static void install_mouse_server() {
  uint32_t is = mouse_server;
  wr8(is + 8, 2);                  // ln_Type = NT_INTERRUPT
  wr8(is + 9, 0);                  // ln_Pri
  wr32(is + 10, 0x2a5d8);          // ln_Name
  wr32(is + 18, mouse_server_code);// is_Code
  os::AddIntServer(5, is);         // INTB_VERTB
}
NATIVE_C(0x295ae, install_mouse_server, false);

// $29284: latch the current JOY0DAT counters and start the server.
static void init_mouse(int32_t, int32_t) {
  uint16_t joy = rd16(0xdff00a);
  mouse_last_x = joy & 0xFF;
  mouse_last_y = uint16_t((int16_t(joy) >> 8) & 0xFF);
  install_mouse_server();
}
NATIVE_C(0x29284, init_mouse, false);

// $295e4: remove the mouse server again.
static void remove_mouse_server() { os::RemIntServer(5, mouse_server); }
NATIVE_C(0x295e4, remove_mouse_server, false);

// $295f8: amiga.lib MakePort-style initialisation of a MsgPort in place
// (signal bit allocated, owner task, empty message list).
static void make_port(uint32_t port) {
  wr8(port + 14, 0);                          // mp_Flags = PA_SIGNAL
  wr8(port + 9, 0);                           // ln_Pri
  wr32(port + 10, 0);                         // ln_Name
  wr8(port + 8, 4);                           // ln_Type = NT_MSGPORT
  int32_t sig = int32_t(sub_69150(uint32_t(-1)));   // AllocSignal(-1)
  wr8(port + 15, uint8_t(sig));               // mp_SigBit
  wr32(port + 16, os::FindTask(0));           // mp_SigTask
  sub_69260(port + 20);                       // NewList(&mp_MsgList)
}
NATIVE_C(0x295f8, make_port, false);

// ---- picture planes ------------------------------------------------------------

// $28a38: free the 5th plane of the front and cockpit pictures.
static void free_picture_fifth_planes() {
  os::FreeRaster(rd32(uint32_t(front_bitmap) + 24), front_bmhd_w, front_bmhd_h);
  os::FreeRaster(rd32(uint32_t(cockpit_bitmap) + 24), cockpit_bmhd_w, cockpit_bmhd_h);
}
NATIVE_C(0x28a38, free_picture_fifth_planes, false);

// $28a8a: remember the splash picture's planes (once the title flag is set).
static void save_splash_planes() {
  if (title_flag_5ab46 == 0) return;
  uint32_t bm = splash_bitmap;
  for (int i = 0; i < 5; ++i) wr32(0x2cfbc + uint32_t(i) * 4, rd32(bm + 8 + uint32_t(i) * 4));
}
NATIVE_C(0x28a8a, save_splash_planes, false);

// $28c82: memcpy-like byte copy (n, src, dst).
static void copy_bytes(int32_t n, uint32_t src, uint32_t dst) {
  for (int32_t i = 0; i < n; ++i) wr8(dst + uint32_t(i), rd8(src + uint32_t(i)));
}
NATIVE_C(0x28c82, copy_bytes, false);

// $28cb0: dst[i] |= src[i] for i < n.
static void or_bytes(int32_t n, uint32_t src, uint32_t dst) {
  for (int32_t i = 0; i < n; ++i) wr8(dst + uint32_t(i), rd8(dst + uint32_t(i)) | rd8(src + uint32_t(i)));
}
NATIVE_C(0x28cb0, or_bytes, false);

// $28c2c: union of four bitplanes into `out`.
static void or_four_planes(int32_t n, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3, uint32_t out) {
  copy_bytes(n, p0, out);
  or_bytes(n, p1, out);
  or_bytes(n, p2, out);
  or_bytes(n, p3, out);
}
NATIVE_C(0x28c2c, or_four_planes, false);

// $28ada: set up the cockpit picture's plane pointers and the combined
// "any of planes 0-3" mask of the front picture (used for window masking).
static void setup_cockpit_planes() {
  uint32_t ck = cockpit_bitmap;
  constexpr uint32_t dst[4] = {0x2cfe8, 0x2cfec, 0x2cff0, 0x2cff4};
  for (int i = 0; i < 4; ++i) wr32(dst[i], rd32(ck + 8 + uint32_t(i) * 4));
  uint32_t scratch = os::AllocMem(32, os::MEMF_CHIP | os::MEMF_CLEAR);
  wr32(0x2d028, scratch);
  if (scratch == 0) sub_6620c(uint32_t(-1000));
  for (int i = 0; i < 4; ++i) wr32(0x2d02c + uint32_t(i) * 4, rd32(dst[i]) + 720);
  int32_t size = int32_t((uint32_t(front_bmhd_w) * uint32_t(front_bmhd_h)) >> 3);
  uint32_t mask = os::AllocMem(uint32_t(size), os::MEMF_CHIP | os::MEMF_CLEAR);
  wr32(0x2cffc, mask);
  if (mask == 0) sub_6620c(uint32_t(-1000));
  uint32_t fr = front_bitmap;
  uint32_t planes[4];
  for (int i = 0; i < 4; ++i) {
    planes[i] = rd32(fr + 8 + uint32_t(i) * 4);
    wr32(0x2d018 + uint32_t(i) * 4, planes[i]);
  }
  or_four_planes(size, planes[0], planes[1], planes[2], planes[3], mask);
}
NATIVE_C(0x28ada, setup_cockpit_planes, false);

}  // namespace native
