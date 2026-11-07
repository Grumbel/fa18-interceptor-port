// F/A-18 Interceptor — SDL2 front end.
#include "amiga/machine.hpp"
#include "control_help.hpp"
#include "font6x10.hpp"
#include "native/controls.hpp"
#include "native/gl_renderer.hpp"
#include "native/cockpit_assets.hpp"
#include "native/remaster.hpp"
#include "native/mesh_viewer.hpp"
#include "frontend_hotkeys.hpp"
#include "graphics_toggle.hpp"
#include "native/terrain.hpp"
#include "native/world.hpp"
#include "original_menu_font.hpp"
#include "savestate_browser.hpp"
#include "stats_overlay.hpp"

#include <SDL.h>
#if defined(__ANDROID__)
#include <SDL_system.h>
#endif
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Scripted input for automated runs:  <frame> <command> [args]
//   key <SDL key name> [down|up|tap]   joy <subset of "udlrf">
//   mouse <dx> <dy>   lmb|rmb <0|1>   shot <file.ppm>   quit
//   poke <hexaddr> <hexvalue> [b|w|l]   (force a game variable)
//   stats on|off   (statistics overlay; "shot" then also writes FILE.stats.ppm)
//   blitter on|off   (suppress original Amiga blitter destination writes)
struct ScriptEvent {
  int frame;
  std::string cmd;
  std::vector<std::string> args;
};

std::vector<ScriptEvent> load_script(const std::string& path) {
  std::vector<ScriptEvent> ev;
  std::ifstream in(path);
  std::string line;
  int last = 0;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string f;
    ScriptEvent e;
    ss >> f >> e.cmd;
    // "+N" is relative to the previous event
    e.frame = f[0] == '+' ? last + std::atoi(f.c_str() + 1) : std::atoi(f.c_str());
    last = e.frame;
    std::string a;
    while (ss >> a) e.args.push_back(a);
    ev.push_back(e);
  }
  return ev;
}

int amiga_rawkey(SDL_Scancode sc) {
  switch (sc) {
    case SDL_SCANCODE_GRAVE: return 0x00;
    case SDL_SCANCODE_1: return 0x01; case SDL_SCANCODE_2: return 0x02;
    case SDL_SCANCODE_3: return 0x03; case SDL_SCANCODE_4: return 0x04;
    case SDL_SCANCODE_5: return 0x05; case SDL_SCANCODE_6: return 0x06;
    case SDL_SCANCODE_7: return 0x07; case SDL_SCANCODE_8: return 0x08;
    case SDL_SCANCODE_9: return 0x09; case SDL_SCANCODE_0: return 0x0A;
    case SDL_SCANCODE_MINUS: return 0x0B; case SDL_SCANCODE_EQUALS: return 0x0C;
    case SDL_SCANCODE_BACKSLASH: return 0x0D;
    case SDL_SCANCODE_Q: return 0x10; case SDL_SCANCODE_W: return 0x11;
    case SDL_SCANCODE_E: return 0x12; case SDL_SCANCODE_R: return 0x13;
    case SDL_SCANCODE_T: return 0x14; case SDL_SCANCODE_Y: return 0x15;
    case SDL_SCANCODE_U: return 0x16; case SDL_SCANCODE_I: return 0x17;
    case SDL_SCANCODE_O: return 0x18; case SDL_SCANCODE_P: return 0x19;
    case SDL_SCANCODE_LEFTBRACKET: return 0x1A; case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
    case SDL_SCANCODE_A: return 0x20; case SDL_SCANCODE_S: return 0x21;
    case SDL_SCANCODE_D: return 0x22; case SDL_SCANCODE_F: return 0x23;
    case SDL_SCANCODE_G: return 0x24; case SDL_SCANCODE_H: return 0x25;
    case SDL_SCANCODE_J: return 0x26; case SDL_SCANCODE_K: return 0x27;
    case SDL_SCANCODE_L: return 0x28; case SDL_SCANCODE_SEMICOLON: return 0x29;
    case SDL_SCANCODE_APOSTROPHE: return 0x2A;
    case SDL_SCANCODE_NONUSBACKSLASH: return 0x30;
    case SDL_SCANCODE_Z: return 0x31; case SDL_SCANCODE_X: return 0x32;
    case SDL_SCANCODE_C: return 0x33; case SDL_SCANCODE_V: return 0x34;
    case SDL_SCANCODE_B: return 0x35; case SDL_SCANCODE_N: return 0x36;
    case SDL_SCANCODE_M: return 0x37; case SDL_SCANCODE_COMMA: return 0x38;
    case SDL_SCANCODE_PERIOD: return 0x39; case SDL_SCANCODE_SLASH: return 0x3A;
    case SDL_SCANCODE_SPACE: return 0x40; case SDL_SCANCODE_BACKSPACE: return 0x41;
    case SDL_SCANCODE_TAB: return 0x42; case SDL_SCANCODE_KP_ENTER: return 0x43;
    case SDL_SCANCODE_RETURN: return 0x44; case SDL_SCANCODE_ESCAPE: return 0x45;
    case SDL_SCANCODE_DELETE: return 0x46; case SDL_SCANCODE_KP_MINUS: return 0x4A;
    case SDL_SCANCODE_UP: return 0x4C; case SDL_SCANCODE_DOWN: return 0x4D;
    case SDL_SCANCODE_RIGHT: return 0x4E; case SDL_SCANCODE_LEFT: return 0x4F;
    case SDL_SCANCODE_F1: return 0x50; case SDL_SCANCODE_F2: return 0x51;
    case SDL_SCANCODE_F3: return 0x52; case SDL_SCANCODE_F4: return 0x53;
    case SDL_SCANCODE_F5: return 0x54; case SDL_SCANCODE_F6: return 0x55;
    case SDL_SCANCODE_F7: return 0x56; case SDL_SCANCODE_F8: return 0x57;
    case SDL_SCANCODE_F9: return 0x58; case SDL_SCANCODE_F10: return 0x59;
    case SDL_SCANCODE_KP_0: return 0x0F; case SDL_SCANCODE_KP_1: return 0x1D;
    case SDL_SCANCODE_KP_2: return 0x1E; case SDL_SCANCODE_KP_3: return 0x1F;
    case SDL_SCANCODE_KP_4: return 0x2D; case SDL_SCANCODE_KP_5: return 0x2E;
    case SDL_SCANCODE_KP_6: return 0x2F; case SDL_SCANCODE_KP_7: return 0x3D;
    case SDL_SCANCODE_KP_8: return 0x3E; case SDL_SCANCODE_KP_9: return 0x3F;
    case SDL_SCANCODE_KP_PERIOD: return 0x3C; case SDL_SCANCODE_KP_DIVIDE: return 0x5C;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x5D; case SDL_SCANCODE_KP_PLUS: return 0x5E;
    case SDL_SCANCODE_HOME: return 0x5F;  // HELP
    case SDL_SCANCODE_LSHIFT: return 0x60; case SDL_SCANCODE_RSHIFT: return 0x61;
    case SDL_SCANCODE_CAPSLOCK: return 0x62; case SDL_SCANCODE_LCTRL: return 0x63;
    case SDL_SCANCODE_LALT: return 0x64; case SDL_SCANCODE_RALT: return 0x65;
    case SDL_SCANCODE_LGUI: return 0x66; case SDL_SCANCODE_RGUI: return 0x67;
    default: return -1;
  }
}

bool init_gl(SDL_Window* visible = nullptr);
bool gl_failed = false;  // no OpenGL context: the menu skips the GL entries

class SdlHost : public amiga::Host {
public:
  SDL_Window* win = nullptr;
  SDL_Renderer* ren = nullptr;
  SDL_Texture* tex = nullptr;
  SDL_AudioDeviceID audio_dev = 0;
  amiga::Machine* machine = nullptr;
  SDL_GameController* pad = nullptr;
  bool mouse_grab = false;
  bool fast = false;
  bool widescreen = false;
  bool direct_gl = false;   // --gl-direct / FA18_GL_DIRECT=1
  int render_fps = 60;  // GL-only redraws; emulated physics is always 60 Hz
  uint64_t last_gl_serial = 0;
  uint64_t extra_gl_presented = 0;
  uint64_t extra_gl_missed = 0;
  uint32_t present_report_ms = 0;
  int present_report_count = 0;
  float observed_fps = 0.f;
  int last_gl_tick = 0, gl_interval_ticks = 4;
  bool quit = false;
  uint64_t next_frame = 0;
  int screenshot_frame = -1;
  std::string screenshot_path;
  int frame_no = 0;
  int quit_after = -1;
  std::vector<ScriptEvent> script;
  size_t script_pos = 0;
  std::vector<std::pair<int, int>> pending_up;  // frame, rawkey
  bool sjoy[5] = {};
  // Gamepad: previous button mask for edge detection; held rawkeys for release.
  uint32_t pad_prev_btns = 0;
  int pad_prev_hat = -1;      // pie menu: last direction while a shoulder is held (-1 none, 8 neutral)
  int pad_throttle_cd = 0;    // frames until next throttle step
  FrontendHotkeys debug_hotkeys;
  bool blitter_hotkey_held = false;
  GraphicsToggle graphics_toggle;
  bool pad_held_key[128] = {};
  bool pad_blocked[SDL_CONTROLLER_BUTTON_MAX] = {};  // swallowed until released
  // Debug overlay with game world statistics (stats_overlay.cpp), drawn at
  // 3x the logical size so the text stays readable.
  bool stats_on = false;
  bool fps_on = false;
  static constexpr int STATS_W = amiga::FB_W * 3, STATS_H = amiga::FB_H * 6 / 5 * 3;
  SDL_Texture* stats_tex = nullptr;
  std::vector<uint32_t> stats_buf;
  std::vector<uint8_t> hud_rgba;
  int hud_frame = -1;
  std::vector<uint32_t> pause_ui;  // 960x720 transparent, original-view centred
  std::vector<uint8_t> pause_rgba;
  std::vector<uint32_t> view_buf;
  remaster::GpuLayers gpu_layers;
  int gpu_layers_frame = -1;

  bool init(int scale) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
      fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
      return false;
    }
#if defined(__EMSCRIPTEN__)
    // Let ASYNCIFY sleep from SDL_Delay / emscripten_sleep; focus keyboard on canvas.
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_ASYNCIFY, "1");
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
#endif
    const int win_h = amiga::FB_H * scale * 6 / 5;
    const int win_w = widescreen ? win_h * 16 / 9 : amiga::FB_W * scale;
    const Uint32 window_flags = SDL_WINDOW_RESIZABLE | (direct_gl ? SDL_WINDOW_OPENGL : 0);
    win = SDL_CreateWindow(machine->original_blitter_writes() ?
                           "F/A-18 Interceptor" : "F/A-18 Interceptor [AMIGA BLITTER OFF]",
                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           win_w, win_h, window_flags);
    if (!win) return false;
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) {
      fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
      return false;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    // NTSC lores pixels are slightly taller than wide; 320x240 approximates.
    SDL_RenderSetLogicalSize(ren, amiga::FB_W, amiga::FB_H * 6 / 5);
#if defined(__EMSCRIPTEN__)
    // Force the canvas drawing buffer to the window size (CSS handles display).
    SDL_SetWindowSize(win, win_w, win_h);
    {
      SDL_RendererInfo ri{};
      if (SDL_GetRendererInfo(ren, &ri) == 0)
        fprintf(stderr, "emscripten window %dx%d renderer=%s\n", win_w, win_h,
                ri.name ? ri.name : "?");
      else
        fprintf(stderr, "emscripten window %dx%d\n", win_w, win_h);
    }
#endif
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                            amiga::FB_W, amiga::FB_H);
    SDL_AudioSpec want{}, have{};
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    audio_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audio_dev) SDL_PauseAudioDevice(audio_dev, 0);
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
      if (!script.empty()) break;  // scripted runs are deterministic: no real pad
      else if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }
    next_frame = SDL_GetPerformanceCounter();
    return true;
  }

  void set_grab(bool g) {
    mouse_grab = g;
    SDL_SetRelativeMouseMode(g ? SDL_TRUE : SDL_FALSE);
  }

  void set_graphics_mode(int scale, bool use_gl) {
    graphics_toggle.remember({scale, use_gl});
    glr::set_enabled(use_gl);
    machine->set_hires_scale(scale);
  }

  void toggle_graphics() {
    GraphicsMode mode = graphics_toggle.next({machine->hires_scale, glr::active()});
    if (mode.scale > 0 && mode.opengl && !glr::ready() &&
        (gl_failed || !init_gl(direct_gl ? win : nullptr))) {
      gl_failed = true;
      mode.opengl = false;
    }
    set_graphics_mode(mode.scale, mode.opengl);
  }

  void set_original_blitter_writes(bool enabled) {
    if (machine->original_blitter_writes() == enabled) return;
    machine->set_original_blitter_writes(enabled);
    SDL_SetWindowTitle(win, enabled ? "F/A-18 Interceptor" :
                                     "F/A-18 Interceptor [AMIGA BLITTER OFF]");
    hud_frame = -1;
    fprintf(stderr, "graphics: original Amiga blitter writes %s (Ctrl+B, %llu suppressed)\n",
            enabled ? "ON" : "OFF",
            (unsigned long long)machine->suppressed_blitter_words());
  }

  void handle_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_QUIT: quit = true; break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
          bool down = e.type == SDL_KEYDOWN;
          SDL_Scancode sc = e.key.keysym.scancode;
          // Ctrl+B is a host-side comparison switch, never a guest B key.
          // Remember the down event in case Ctrl is released before B.
          if (sc == SDL_SCANCODE_B &&
              ((e.key.keysym.mod & KMOD_CTRL) || blitter_hotkey_held)) {
            if (down && !e.key.repeat) {
              blitter_hotkey_held = true;
              set_original_blitter_writes(!machine->original_blitter_writes());
            }
            if (!down) blitter_hotkey_held = false;
            break;
          }
          if (sc == SDL_SCANCODE_F12 && down) { set_grab(!mouse_grab); break; }
          if (sc == SDL_SCANCODE_INSERT && down) {
            menu_request = true;
            break;
          }
          // F9/F10 are game throttle keys; only Ctrl+F9/F10 belong to us.
          auto hk = debug_hotkeys.key(
              sc == SDL_SCANCODE_F9 ? FrontendKey::F9 :
              sc == SDL_SCANCODE_F10 ? FrontendKey::F10 : FrontendKey::Other,
              down, (e.key.keysym.mod & KMOD_CTRL) != 0, e.key.repeat != 0);
          if (hk == FrontendAction::OpenMenu) { menu_request = true; break; }
          if (hk == FrontendAction::OpenModels) { mesh_viewer_request = true; break; }
          if (hk == FrontendAction::Consumed) break;
          if (sc == SDL_SCANCODE_F11) {
            if (down && !e.key.repeat) {
              if (e.key.keysym.mod & KMOD_CTRL) {
                toggle_graphics();
              } else {
                Uint32 fl = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(win, fl ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
              }
            }
            break;
          }
          if (sc == SDL_SCANCODE_PAUSE && down) { fast = !fast; break; }
          if (sc == SDL_SCANCODE_F4 && down && (e.key.keysym.mod & KMOD_ALT)) { quit = true; break; }
          if (e.key.repeat) break;
          int rk = amiga_rawkey(sc);
          if (rk >= 0) machine->key_event(uint8_t(rk), down);
          break;
        }
        case SDL_MOUSEMOTION:
          if (mouse_grab) machine->mouse_move(e.motion.xrel, e.motion.yrel);
          break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
          bool down = e.type == SDL_MOUSEBUTTONDOWN;
          // Mouse grab is opt-in (--grab or F12); do not steal the pointer on click.
          if (e.button.button == SDL_BUTTON_LEFT) machine->mouse_button(0, down);
          if (e.button.button == SDL_BUTTON_RIGHT) machine->mouse_button(1, down);
          break;
        }
        case SDL_CONTROLLERDEVICEADDED:
          if (!pad && script.empty()) pad = SDL_GameControllerOpen(e.cdevice.which);
          break;
      }
    }
    run_script();
    // Joystick (port 2) + expanded gamepad → keyboard shortcuts.
    // The original only supports a digital stick (JOY1DAT) — no analog pots.
    bool up = sjoy[0], down = sjoy[1], left = sjoy[2], right = sjoy[3], fire = sjoy[4];
    if (pad) poll_gamepad(up, down, left, right, fire);
    machine->joy_state(up, down, left, right, fire);
  }

  // Inject a one-frame key tap (down now, up next frame via pending_up).
  void pad_tap(int rawkey) {
    if (rawkey < 0) return;
    machine->key_event(uint8_t(rawkey), true);
    pending_up.push_back({frame_no + 2, rawkey});
  }

  void pad_set_held(int rawkey, bool down) {
    if (rawkey < 0 || rawkey >= 128) return;
    if (down == pad_held_key[rawkey]) return;
    pad_held_key[rawkey] = down;
    machine->key_event(uint8_t(rawkey), down);
  }

  // Direction index from stick/dpad: 0=N,1=NE,2=E,...,7=NW, 8=center, -1=none.
  static int pad_dir8(int x, int y, bool du, bool dd, bool dl, bool dr) {
    constexpr int dead = 16000;
    int hx = 0, hy = 0;
    if (x < -dead || dl) hx = -1;
    else if (x > dead || dr) hx = 1;
    if (y < -dead || du) hy = -1;
    else if (y > dead || dd) hy = 1;
    if (hx == 0 && hy == 0) return 8;  // center / neutral
    // 8-way: N=0, NE=1, E=2, SE=3, S=4, SW=5, W=6, NW=7
    static const int tbl[3][3] = {
      {7, 0, 1},
      {6, 8, 2},
      {5, 4, 3},
    };
    return tbl[hy + 1][hx + 1];
  }

  void poll_gamepad(bool& up, bool& down, bool& left, bool& right, bool& fire) {
    auto btn = [&](SDL_GameControllerButton b) {
      bool down = SDL_GameControllerGetButton(pad, b) != 0;
      if (pad_blocked[b]) {          // pressed while the menu was open
        if (!down) pad_blocked[b] = false;
        return false;
      }
      return down;
    };
    auto axis = [&](SDL_GameControllerAxis a) {
      return int(SDL_GameControllerGetAxis(pad, a));
    };

    const bool lb = btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    const bool rb = btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    const bool a = btn(SDL_CONTROLLER_BUTTON_A);
    const bool b = btn(SDL_CONTROLLER_BUTTON_B);
    const bool x = btn(SDL_CONTROLLER_BUTTON_X);
    const bool y = btn(SDL_CONTROLLER_BUTTON_Y);
    const bool start = btn(SDL_CONTROLLER_BUTTON_START);
    const bool back = btn(SDL_CONTROLLER_BUTTON_BACK);
    const bool ls = btn(SDL_CONTROLLER_BUTTON_LEFTSTICK);
    const bool rs = btn(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    const bool du = btn(SDL_CONTROLLER_BUTTON_DPAD_UP);
    const bool dd = btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    const bool dl = btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    const bool dr = btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);

    int lx = axis(SDL_CONTROLLER_AXIS_LEFTX);
    int ly = axis(SDL_CONTROLLER_AXIS_LEFTY);
    int rx = axis(SDL_CONTROLLER_AXIS_RIGHTX);
    int ry = axis(SDL_CONTROLLER_AXIS_RIGHTY);
    int lt = axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    int rt = axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);

    // --- flight stick (digital JOY1DAT); suppressed while a layer is held ---
    machine->analog_active = false;
    if (!lb && !rb) {
      constexpr int dead = 12000;
      if (machine->analog_enabled) {
        // Remaster: left stick is analog (radial dead zone, rescaled);
        // the d-pad stays digital.
        float fx = float(lx) / 32767.0f, fy = float(ly) / 32767.0f;
        float m = std::sqrt(fx * fx + fy * fy);
        constexpr float dz = 0.12f;
        if (m > dz) {
          float s = std::min(1.0f, (m - dz) / (1.0f - dz)) / m;
          machine->analog_x = fx * s;
          machine->analog_y = fy * s;
          machine->analog_active = true;
        }
      } else {
        up |= ly < -dead;
        down |= ly > dead;
        left |= lx < -dead;
        right |= lx > dead;
      }
      up |= du;
      down |= dd;
      left |= dl;
      right |= dr;
    }
    fire |= a;

    // --- edge-triggered face / menu buttons ---
    uint32_t now = 0;
    auto bit = [&](int i, bool v) { if (v) now |= (1u << i); };
    bit(0, a); bit(1, b); bit(2, x); bit(3, y);
    bit(4, start); bit(5, back); bit(6, ls); bit(7, rs);
    bit(8, lb); bit(9, rb);
    uint32_t pressed = now & ~pad_prev_btns;
    uint32_t released = pad_prev_btns & ~now;
    pad_prev_btns = now;

    // Primary (no layer): face + sticks + start/back
    if (!lb && !rb) {
      if (pressed & (1u << 1)) pad_tap(0x44);       // B = Return (weapon)
      if (pressed & (1u << 2)) pad_tap(0x14);       // X = T (target)
      if (pressed & (1u << 3)) pad_tap(0x37);       // Y = M (map)
      if (pressed & (1u << 4)) pad_tap(0x19);       // Start = P (pause)
      if (pressed & (1u << 5)) menu_request = true;  // Back = debug menu
      if (pressed & (1u << 6)) pad_tap(0x41);       // LS click = brakes
      if (pressed & (1u << 7)) pad_tap(0x24);       // RS click = gear
    }

    // Hold layers act as pie menus: while LB (systems) or RB (views) is
    // held, pushing the stick or d-pad into a direction fires that entry
    // immediately; clicking the left stick fires the centre entry.  Face
    // buttons are shortcuts while LB is held.  LB wins over RB.
    {
      // N=R, NE=J, E=T, SE=C, S=M, SW=F, W=H, NW=A, center=G
      static const int sys_keys[9] = {
        0x13, 0x26, 0x14, 0x33, 0x37, 0x23, 0x25, 0x20, 0x24
      };
      // Keypad 8,9,6,3,2,1,4,7,5
      static const int view_keys[9] = {
        0x3E, 0x3F, 0x2F, 0x1F, 0x1E, 0x1D, 0x2D, 0x3D, 0x2E
      };
      const int* keys = lb ? sys_keys : (rb ? view_keys : nullptr);
      if (keys) {
        int dir = pad_dir8(lx, ly, du, dd, dl, dr);  // 8 = neutral
        // a direction already held when the shoulder goes down does not fire
        if (pad_prev_hat >= 0 && dir != 8 && dir != pad_prev_hat) pad_tap(keys[dir]);
        pad_prev_hat = dir;
        if (pressed & (1u << 6)) pad_tap(keys[8]);   // stick click = centre entry
        if (lb) {
          if (pressed & (1u << 0)) pad_tap(0x25);  // A = H (HUD)
          if (pressed & (1u << 1)) pad_tap(0x26);  // B = J (ECM)
          if (pressed & (1u << 2)) pad_tap(0x33);  // X = C (chaff)
          if (pressed & (1u << 3)) pad_tap(0x23);  // Y = F (flare)
        } else if (pressed & (1u << 0)) {
          pad_tap(0x43);  // RB + A = keypad Enter (outside view)
        }
      } else {
        pad_prev_hat = -1;
      }
    }

    // Throttle on triggers: RT = increase (+ / F10), LT = decrease (- / F1)
    // Repeat every ~10 frames while held past the threshold.
    if (pad_throttle_cd > 0) --pad_throttle_cd;
    if (pad_throttle_cd == 0) {
      if (rt > 20000) { pad_tap(0x0C); pad_throttle_cd = 8; }       // =
      else if (lt > 20000) { pad_tap(0x0B); pad_throttle_cd = 8; }  // -
    }

    // Right stick: rudder (held while deflected)
    constexpr int rdead = 14000;
    pad_set_held(0x38, rx < -rdead);  // comma = rudder left
    pad_set_held(0x39, rx > rdead);   // period = rudder right
    // Right-stick Y: zoom [ ]
    pad_set_held(0x1A, ry < -rdead);  // [
    pad_set_held(0x1B, ry > rdead);   // ]
  }

  int tex_w = amiga::FB_W, tex_h = amiga::FB_H;

  // ---- debug menu (Back button / Insert) -------------------------------------
  bool menu_request = false;
  bool mesh_viewer_request = false;
  std::string menu_status;

  void draw_text(std::vector<uint32_t>& cv, int w, int h, int x, int y, const std::string& t,
                 int sc, uint32_t col) {
    for (size_t i = 0; i < t.size(); ++i) {
      int c = uint8_t(t[i]);
      if (c < 32 || c > 126) c = '?';
      const uint8_t* g = &font6x10::glyphs[(c - 32) * font6x10::H];
      for (int gy = 0; gy < font6x10::H; ++gy)
        for (int gx = 0; gx < font6x10::W; ++gx)
          if (g[gy] & (0x20 >> gx))
            for (int yy = 0; yy < sc; ++yy)
              for (int xx = 0; xx < sc; ++xx) {
                int px = x + (int(i) * font6x10::W + gx) * sc + xx, py = y + gy * sc + yy;
                if (px >= 0 && px < w && py >= 0 && py < h) cv[size_t(py) * size_t(w) + size_t(px)] = col;
              }
    }
  }

  // First native flight-interface element. Draw the Pause heading with
  // resolution-independent antialiased strokes instead of magnifying the
  // original 7-pixel-tall blitter lettering. Its low-res title is removed
  // by the GL compositor, while the rest of the original panel stays intact.
  void prepare_pause_ui() {
    if (!pause_ui.empty()) return;
    pause_ui.assign(size_t(STATS_W) * STATS_H, 0);
    // Geometry/coverage texture only: tint from the live copper pen 9 at
    // presentation time, so colour changes do not require a GPU re-upload.
    constexpr uint32_t amber = 0x00FFFFFFu;
    const std::string label = "- PAUSED -";
    const float advance = 20.f;
    const float left = (STATS_W - advance * label.size()) * .5f;
    const float top = 155.f;
    auto line = [&](float x1, float y1, float x2, float y2) {
      const int xlo = std::max(0, int(std::floor(std::min(x1, x2) - 2.5f)));
      const int xhi = std::min(STATS_W - 1, int(std::ceil(std::max(x1, x2) + 2.5f)));
      const int ylo = std::max(0, int(std::floor(std::min(y1, y2) - 2.5f)));
      const int yhi = std::min(STATS_H - 1, int(std::ceil(std::max(y1, y2) + 2.5f)));
      const float dx = x2 - x1, dy = y2 - y1;
      const float length2 = dx * dx + dy * dy;
      for (int y = ylo; y <= yhi; ++y)
        for (int x = xlo; x <= xhi; ++x) {
          const float t = length2 > 0 ? std::clamp(((x + .5f - x1) * dx +
                                  (y + .5f - y1) * dy) / length2, 0.f, 1.f) : 0.f;
          const float dist = std::hypot(x + .5f - (x1 + dx * t),
                                        y + .5f - (y1 + dy * t));
          const uint32_t a = uint32_t(std::lround(255.f *
                                std::clamp(1.8f - dist, 0.f, 1.f)));
          uint32_t& pixel = pause_ui[size_t(y) * STATS_W + size_t(x)];
          if (a > pixel >> 24) pixel = (a << 24) | amber;
        }
    };
    for (size_t i = 0; i < label.size(); ++i) {
      const float x = left + float(i) * advance, y = top;
      auto stroke = [&](int ax, int ay, int bx, int by) {
        line(x + ax, y + ay, x + bx, y + by);
      };
      switch (label[i]) {
        case '-': stroke(3, 10, 14, 10); break;
        case 'P': stroke(2, 2, 2, 20); stroke(2, 2, 13, 2);
                  stroke(13, 2, 13, 10); stroke(2, 10, 13, 10); break;
        case 'A': stroke(2, 20, 2, 6); stroke(2, 6, 6, 2);
                  stroke(6, 2, 11, 2); stroke(11, 2, 14, 6);
                  stroke(14, 6, 14, 20); stroke(2, 12, 14, 12); break;
        case 'U': stroke(2, 2, 2, 17); stroke(2, 17, 5, 20);
                  stroke(5, 20, 11, 20); stroke(11, 20, 14, 17);
                  stroke(14, 17, 14, 2); break;
        case 'S': stroke(14, 2, 4, 2); stroke(4, 2, 2, 5);
                  stroke(2, 5, 2, 9); stroke(2, 9, 14, 12);
                  stroke(14, 12, 14, 17); stroke(14, 17, 11, 20);
                  stroke(11, 20, 2, 20); break;
        case 'E': stroke(14, 2, 2, 2); stroke(2, 2, 2, 20);
                  stroke(2, 11, 12, 11); stroke(2, 20, 14, 20); break;
        case 'D': stroke(2, 2, 2, 20); stroke(2, 2, 10, 2);
                  stroke(10, 2, 14, 6); stroke(14, 6, 14, 16);
                  stroke(14, 16, 10, 20); stroke(10, 20, 2, 20); break;
        default: break;
      }
    }
    pause_rgba.resize(pause_ui.size() * 4);
    for (size_t i = 0; i < pause_ui.size(); ++i) {
      const uint32_t p = pause_ui[i];
      pause_rgba[4 * i] = uint8_t(p >> 16);
      pause_rgba[4 * i + 1] = uint8_t(p >> 8);
      pause_rgba[4 * i + 2] = uint8_t(p);
      pause_rgba[4 * i + 3] = uint8_t(p >> 24);
    }
  }

  void blend_pause_ui(std::vector<uint32_t>& fb, int side, int scale) {
    if (!remaster::pause_screen_active() || fb.empty()) return;
    const remaster::NativeHudState hud = remaster::native_hud_state(*machine);
    if (!hud.pause_title) return;
    prepare_pause_ui();
    const int pitch = (amiga::FB_W + 2 * side) * scale;
    // Only sample the heading's small footprint rather than its large
    // transparent texture; this also keeps high-refresh Pause inexpensive.
    for (int y = 42 * scale; y < 51 * scale; ++y) {
      const int sy = y * STATS_H / (200 * scale);
      for (int x = 116 * scale; x < 204 * scale; ++x) {
        const int sx = x * STATS_W / (320 * scale);
        const uint32_t src = pause_ui[size_t(sy) * STATS_W + size_t(sx)];
        const uint32_t a = src >> 24;
        if (!a) continue;
        uint32_t& dst = fb[size_t(y) * pitch + size_t(side * scale + x)];
        uint32_t out = 0xFF000000u;
        for (int shift = 0; shift < 24; shift += 8) {
          const uint32_t c = (((hud.pause_rgb >> shift) & 255) * a +
                              ((dst >> shift) & 255) * (255 - a) + 127) / 255;
          out |= c << shift;
        }
        dst = out;
      }
    }
  }

  void run_mesh_viewer_standalone(const uint32_t* /*bg*/, int w, int h) {
    SDL_RenderSetLogicalSize(ren, amiga::FB_W, amiga::FB_H * 6 / 5);
      std::vector<uint32_t> cv(size_t(w) * size_t(h));
      std::vector<uint8_t> exe = machine->disk_read("F-18 Interceptor");
      if (exe.empty()) {
        menu_status = "mesh viewer: need ADF with F-18 Interceptor";
        return;
      }
      auto models = mesh_viewer::load_models(exe);
      if (models.empty()) {
        menu_status = "mesh viewer: no models";
        return;
      }
      uint16_t live_pal[16];
      for (int i = 0; i < 16; ++i) live_pal[i] = machine->color_reg(i);
      static mesh_viewer::ViewState vs;  // keep the position between visits
      int axis_cd_v = 0;
      bool view_open = true;
      bool orbiting = false;   // left-drag orbits; right-drag / wheel zooms
      int last_mx = 0, last_my = 0;
      auto text_fn = +[](std::vector<uint32_t>& buf, int bw, int bh, int x, int y,
                         const std::string& t, int scale, uint32_t col) {
        for (size_t i = 0; i < t.size(); ++i) {
          int c = uint8_t(t[i]);
          if (c < 32 || c > 126) c = '?';
          const uint8_t* g = &font6x10::glyphs[(c - 32) * font6x10::H];
          for (int gy = 0; gy < font6x10::H; ++gy)
            for (int gx = 0; gx < font6x10::W; ++gx)
              if (g[gy] & (0x20 >> gx))
                for (int yy = 0; yy < scale; ++yy)
                  for (int xx = 0; xx < scale; ++xx) {
                    int px = x + (int(i) * font6x10::W + gx) * scale + xx;
                    int py = y + gy * scale + yy;
                    if (px >= 0 && px < bw && py >= 0 && py < bh)
                      buf[size_t(py) * size_t(bw) + size_t(px)] = col;
                  }
        }
      };
      auto next_model = [&](int d) {
        vs.model += d;
        vs.lod = 0;
      };
      while (view_open && !quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
          switch (e.type) {
            case SDL_QUIT: quit = true; break;
            case SDL_KEYDOWN:
              switch (e.key.keysym.scancode) {
                case SDL_SCANCODE_LEFT: next_model(-1); break;
                case SDL_SCANCODE_RIGHT: next_model(+1); break;
                case SDL_SCANCODE_PAGEUP: next_model(-10); break;
                case SDL_SCANCODE_PAGEDOWN: next_model(+10); break;
                case SDL_SCANCODE_UP: --vs.lod; break;
                case SDL_SCANCODE_DOWN: ++vs.lod; break;
                case SDL_SCANCODE_A: vs.yaw -= 0.12f; break;
                case SDL_SCANCODE_D: vs.yaw += 0.12f; break;
                case SDL_SCANCODE_W: vs.pitch += 0.08f; break;
                case SDL_SCANCODE_S: vs.pitch -= 0.08f; break;
                case SDL_SCANCODE_TAB: vs.wire = !vs.wire; break;
                case SDL_SCANCODE_P: vs.live_palette = !vs.live_palette; break;
                case SDL_SCANCODE_EQUALS:
                case SDL_SCANCODE_KP_PLUS: vs.zoom *= 1.1f; break;
                case SDL_SCANCODE_MINUS:
                case SDL_SCANCODE_KP_MINUS: vs.zoom /= 1.1f; break;
                case SDL_SCANCODE_ESCAPE:
                case SDL_SCANCODE_INSERT:
                case SDL_SCANCODE_BACKSPACE: view_open = false; break;
                default: break;
              }
              break;
            case SDL_MOUSEBUTTONDOWN:
              if (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT) {
                orbiting = true;
                last_mx = e.button.x;
                last_my = e.button.y;
              }
              break;
            case SDL_MOUSEBUTTONUP:
              if (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)
                orbiting = false;
              break;
            case SDL_MOUSEMOTION:
              if (orbiting || (e.motion.state & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK))) {
                int dx = e.motion.x - last_mx;
                int dy = e.motion.y - last_my;
                last_mx = e.motion.x;
                last_my = e.motion.y;
                // Left button (or unspecified drag): orbit. Right button: zoom.
                if (e.motion.state & SDL_BUTTON_RMASK) {
                  vs.zoom *= (dy < 0 ? 1.02f : 0.98f);
                  if (dx) vs.zoom *= (dx > 0 ? 1.01f : 0.99f);
                } else {
                  vs.yaw += float(dx) * 0.01f;
                  vs.pitch += float(dy) * 0.01f;
                }
              }
              break;
            case SDL_MOUSEWHEEL:
              if (e.wheel.y > 0) vs.zoom *= 1.1f;
              else if (e.wheel.y < 0) vs.zoom /= 1.1f;
              break;
            case SDL_CONTROLLERBUTTONDOWN:
              switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: next_model(-1); break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: next_model(+1); break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: next_model(-10); break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: next_model(+10); break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP: --vs.lod; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: ++vs.lod; break;
                case SDL_CONTROLLER_BUTTON_X: vs.wire = !vs.wire; break;
                case SDL_CONTROLLER_BUTTON_Y: vs.live_palette = !vs.live_palette; break;
                case SDL_CONTROLLER_BUTTON_B:
                case SDL_CONTROLLER_BUTTON_BACK: view_open = false; break;
                default: break;
              }
              break;
            default: break;
          }
        }
        if (pad && axis_cd_v-- <= 0) {
          int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
          int rx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX);
          int ry = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY);
          if (lx < -16000) { next_model(-1); axis_cd_v = 10; }
          else if (lx > 16000) { next_model(+1); axis_cd_v = 10; }
          else axis_cd_v = 0;
          if (std::abs(rx) > 8000) vs.yaw += float(rx) / 32768.f * 0.15f;
          if (std::abs(ry) > 8000) vs.pitch -= float(ry) / 32768.f * 0.10f;
          int lt = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
          int rt = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
          if (rt > 8000) vs.zoom *= 1.02f;
          if (lt > 8000) vs.zoom /= 1.02f;
        }
        vs.pitch = std::clamp(vs.pitch, -1.55f, 1.55f);
        vs.zoom = std::clamp(vs.zoom, 0.2f, 8.f);

        mesh_viewer::render_frame(cv, w, h, models, vs, live_pal, text_fn);
        if (tex_w != w || tex_h != h) {
          if (tex) SDL_DestroyTexture(tex);
          tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
          tex_w = w;
          tex_h = h;
        }
        bool gpu_viewer = false;
        if (direct_gl && glr::ready() && !fast) {
          int dw = 0, dh = 0;
          SDL_GL_GetDrawableSize(win, &dw, &dh);
          gpu_viewer = glr::present_bitmap_gpu(cv.data(), w, h, amiga::FB_W, dw, dh);
          if (gpu_viewer) SDL_GL_SwapWindow(win);
        }
        if (!gpu_viewer) {
          SDL_UpdateTexture(tex, nullptr, cv.data(), w * 4);
          SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
          SDL_RenderClear(ren);
          SDL_RenderCopy(ren, tex, nullptr, nullptr);
          SDL_RenderPresent(ren);
        }
        SDL_Delay(16);
      }
      menu_status = "mesh viewer closed";
  }

  void run_menu(const uint32_t* bg, int w, int h) {
    SDL_RenderSetLogicalSize(ren, amiga::FB_W, amiga::FB_H * 6 / 5);
    menu_request = false;
    if (audio_dev) SDL_PauseAudioDevice(audio_dev, 1);
    enum class Page { Main, Graphics, Controls, Extras };
    Page page = Page::Main;
    int sel = 0;
    int main_sel = 0;
    bool help_open = false;
    int help_page = 0;
    enum class Browser { Closed, Save, Load };
    Browser browser = Browser::Closed;
    std::vector<savestate_browser::Entry> saves;
    int browse_sel = 0;
    bool confirm_overwrite = false;
    constexpr int browse_rows = 11;
    std::vector<uint32_t> cv(size_t(w) * size_t(h));
    const int sc = std::max(1, w / 320);
    original_menu_font::Font game_font;
    game_font.load_executable(machine->disk_read("F-18 Interceptor"));
    auto menu_text = [&](int x, int y, const std::string& text, uint32_t colour) {
      if (game_font.valid()) game_font.draw(cv, w, h, x, y, text, sc, colour);
      else draw_text(cv, w, h, x, y, text, sc, colour);
    };
    bool open = true;
    int axis_cd = 0;
    // pressed/held controller state needs explicit handling: no SDL events
    // reach us for held axes
    auto hires_label = [&] {
      int s = machine->hires_scale;
      return s <= 0 ? std::string("original (320x200)")
                    : std::to_string(s) + "x " + (glr::active() ? "OpenGL" : "software");
    };

    // Offline model viewer: the object models of the type table, decoded
    // from the executable on the ADF.
    auto run_mesh_viewer = [&] { run_mesh_viewer_standalone(bg, w, h); };

    constexpr int MAX_ITEMS = 8;
    static_assert(8 + 16 + MAX_ITEMS * 12 + 3 + 13 + font6x10::H <= 200,
                  "debug menu must fit at the original 320x200 resolution");
    auto item_count = [&] {
      switch (page) {
        case Page::Main: return 8;
        case Page::Graphics: return 8;
        case Page::Controls: return 3;
        case Page::Extras: return 3;
      }
      return 0;
    };
    auto enter_page = [&](Page p) {
      if (page == Page::Main) main_sel = sel;
      page = p;
      sel = 0;
    };
    auto back_page = [&] {
      if (page == Page::Main) return false;
      page = Page::Main;
      sel = main_sel;
      return true;
    };
    auto set_widescreen = [&] {
      widescreen = !widescreen;
      glr::set_deferred_scene_readback(widescreen || render_fps > 60 || direct_gl);
      // When entering widescreen from the regular window, make the extra
      // columns visible immediately. In fullscreen the display owns the size.
      if (!(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP)) {
        int ww = 0, wh = 0;
        SDL_GetWindowSize(win, &ww, &wh);
        if (wh > 0)
          SDL_SetWindowSize(win, widescreen ? wh * 16 / 9 : wh * 4 / 3, wh);
      }
    };
    auto change_render_fps = [&](int delta) {
      constexpr int choices[] = {60, 120, 180, 240};
      int i = 0;
      while (i < 3 && choices[i] != render_fps) ++i;
      render_fps = choices[(i + (delta < 0 ? 3 : 1)) % 4];
      glr::set_deferred_scene_readback(widescreen || render_fps > 60 || direct_gl);
    };
    // Integration tests can inject the same SDL key events that a person
    // uses to navigate the menu; screenshots then verify the resulting page.
    if (const char* keys = getenv("FA18_MENU_NAV"); keys && getenv("FA18_MENU_SHOT")) {
      std::istringstream steps(keys);
      std::string key;
      while (steps >> key) {
        SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;
        if (key == "up") sc = SDL_SCANCODE_UP;
        else if (key == "down") sc = SDL_SCANCODE_DOWN;
        else if (key == "left") sc = SDL_SCANCODE_LEFT;
        else if (key == "right") sc = SDL_SCANCODE_RIGHT;
        else if (key == "enter") sc = SDL_SCANCODE_RETURN;
        else if (key == "esc") sc = SDL_SCANCODE_ESCAPE;
        if (sc != SDL_SCANCODE_UNKNOWN) {
          SDL_Event e{};
          e.type = SDL_KEYDOWN;
          e.key.keysym.scancode = sc;
          SDL_PushEvent(&e);
        }
      }
    }
    auto enter_browser = [&](Browser b) {
      browser = b;
      saves = savestate_browser::list(machine->save_dir);
      browse_sel = 0;
      confirm_overwrite = false;
    };
    auto browse_count = [&] {
      return int(saves.size()) + (browser == Browser::Save ? 2 : 1);
    };
    auto browse_move = [&](int delta) {
      browse_sel = std::clamp(browse_sel + delta, 0, browse_count() - 1);
      confirm_overwrite = false;
    };
    auto activate = [&](int item, int dir) {  // dir: -1/+1 change, 0 select
      if (browser != Browser::Closed) {
        if (dir != 0) { browse_move(dir * browse_rows); return; }
        if (browse_sel == 0) { browser = Browser::Closed; return; }
        if (browser == Browser::Save && browse_sel == 1) {
          const auto path = savestate_browser::new_path(machine->save_dir);
          machine->request_save_state(path.string());
          menu_status = "saving new state...";
          open = false;
          return;
        }
        const int index = browse_sel - (browser == Browser::Save ? 2 : 1);
        if (index >= 0 && index < int(saves.size())) {
          const std::string path = saves[size_t(index)].path.string();
          if (browser == Browser::Save) {
            if (!confirm_overwrite) { confirm_overwrite = true; return; }
            machine->request_save_state(path);
            menu_status = "overwriting saved state...";
          } else {
            machine->request_load_state(path);
            menu_status = "loading saved state...";
          }
          open = false;
        }
        return;
      }
      if (page == Page::Main) {
        // Right opens child pages; Left never accidentally resumes/exits.
        if (dir < 0 || (dir > 0 && (item == 0 || item >= 6))) return;
        switch (item) {
          case 0: open = false; break;
          case 1: enter_page(Page::Graphics); break;
          case 2: enter_page(Page::Controls); break;
          case 3: enter_browser(Browser::Save); break;
          case 4: enter_browser(Browser::Load); break;
          case 5: enter_page(Page::Extras); break;
          case 6: pad_tap(0x45); open = false; break;
          case 7: quit = true; open = false; break;
        }
      } else if (page == Page::Graphics) {
        switch (item) {
          case 0: if (dir <= 0) back_page(); break;
          case 1: {
          // original, 2x-4x software, 2x-4x OpenGL (when a context is available).
          // WebAssembly: OpenGL modes are omitted — a second WebGL context aborts.
#if defined(__EMSCRIPTEN__)
          static const int order[][2] = {{0, 0}, {2, 0}, {3, 0}, {4, 0}};
          const int n = 4;
#else
          static const int order[][2] = {{0, 0}, {2, 0}, {3, 0}, {4, 0}, {2, 1}, {3, 1}, {4, 1}};
          const int n = 7;
#endif
          const int s = machine->hires_scale;
          const int g = s > 0 && glr::active() ? 1 : 0;
          int idx = 0;
          for (int i = 0; i < n; ++i)
            if (order[i][0] == s && order[i][1] == g) idx = i;
          for (int tries = 0; tries < n; ++tries) {
            idx = (idx + (dir == 0 ? 1 : dir) + n) % n;
            if (!order[idx][1] || glr::ready() ||
                (!gl_failed && init_gl(direct_gl ? win : nullptr))) break;
            gl_failed = true;
          }
          set_graphics_mode(order[idx][0], order[idx][1] != 0);
          break;
          }
          case 2: set_widescreen(); break;
          case 3: change_render_fps(dir); break;
          case 4: machine->full_detail = !machine->full_detail; break;
          case 5: stats_on = !stats_on; break;
          case 6: fps_on = !fps_on; break;
          case 7: set_original_blitter_writes(!machine->original_blitter_writes()); break;
        }
      } else if (page == Page::Controls) {
        switch (item) {
          case 0: if (dir <= 0) back_page(); break;
          case 1: machine->analog_enabled = !machine->analog_enabled; break;
          case 2: if (dir >= 0) help_open = true; break;
        }
      } else if (page == Page::Extras) {
        switch (item) {
          case 0: if (dir <= 0) back_page(); break;
          case 1: if (dir == 0) run_mesh_viewer(); break;
          case 2: machine->text_pace = !machine->text_pace; break;
        }
      }
    };
    while (open && !quit) {
      SDL_Event e;
      while (SDL_PollEvent(&e)) {
        switch (e.type) {
          case SDL_QUIT: quit = true; break;
          case SDL_KEYDOWN:
            if (help_open) {
              switch (e.key.keysym.scancode) {
                case SDL_SCANCODE_ESCAPE:
                case SDL_SCANCODE_BACKSPACE:
                case SDL_SCANCODE_H:
                case SDL_SCANCODE_INSERT: help_open = false; break;
                case SDL_SCANCODE_LEFT: help_page = 0; break;
                case SDL_SCANCODE_RIGHT: help_page = 1; break;
                case SDL_SCANCODE_TAB:
                case SDL_SCANCODE_RETURN: help_page ^= 1; break;
                default: break;
              }
              break;
            }
            switch (e.key.keysym.scancode) {
              case SDL_SCANCODE_UP:
                if (browser != Browser::Closed) browse_move(-1);
                else sel = (sel + item_count() - 1) % item_count();
                break;
              case SDL_SCANCODE_DOWN:
                if (browser != Browser::Closed) browse_move(+1);
                else sel = (sel + 1) % item_count();
                break;
              case SDL_SCANCODE_PAGEUP: if (browser != Browser::Closed) browse_move(-browse_rows); break;
              case SDL_SCANCODE_PAGEDOWN: if (browser != Browser::Closed) browse_move(+browse_rows); break;
              case SDL_SCANCODE_HOME: if (browser != Browser::Closed) browse_move(-browse_count()); break;
              case SDL_SCANCODE_END: if (browser != Browser::Closed) browse_move(+browse_count()); break;
              case SDL_SCANCODE_LEFT: activate(sel, -1); break;
              case SDL_SCANCODE_RIGHT: activate(sel, +1); break;
              case SDL_SCANCODE_RETURN: activate(sel, 0); break;
              case SDL_SCANCODE_H: help_open = true; break;
              case SDL_SCANCODE_ESCAPE:
              case SDL_SCANCODE_BACKSPACE:
              case SDL_SCANCODE_INSERT:
                if (confirm_overwrite) confirm_overwrite = false;
                else if (browser != Browser::Closed) browser = Browser::Closed;
                else if (!back_page()) open = false;
                break;
              default: break;
            }
            break;
          case SDL_CONTROLLERBUTTONDOWN:
            if (help_open) {
              switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_B:
                case SDL_CONTROLLER_BUTTON_BACK:
                case SDL_CONTROLLER_BUTTON_START: help_open = false; break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: help_page = 0; break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: help_page = 1; break;
                case SDL_CONTROLLER_BUTTON_A: help_page ^= 1; break;
                default: break;
              }
              break;
            }
            switch (e.cbutton.button) {
              case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (browser != Browser::Closed) browse_move(-1);
                else sel = (sel + item_count() - 1) % item_count();
                break;
              case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (browser != Browser::Closed) browse_move(+1);
                else sel = (sel + 1) % item_count();
                break;
              case SDL_CONTROLLER_BUTTON_DPAD_LEFT: activate(sel, -1); break;
              case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: activate(sel, +1); break;
              case SDL_CONTROLLER_BUTTON_A: activate(sel, 0); break;
              case SDL_CONTROLLER_BUTTON_Y: help_open = true; break;
              case SDL_CONTROLLER_BUTTON_B:
              case SDL_CONTROLLER_BUTTON_BACK:
              case SDL_CONTROLLER_BUTTON_START:
                if (confirm_overwrite) confirm_overwrite = false;
                else if (browser != Browser::Closed) browser = Browser::Closed;
                else if (!back_page()) open = false;
                break;
              default: break;
            }
            break;
          default: break;
        }
      }
      if (pad && !help_open && axis_cd-- <= 0) {  // left stick with auto repeat
        int ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        if (ly < -16000) {
          if (browser != Browser::Closed) browse_move(-1);
          else sel = (sel + item_count() - 1) % item_count();
          axis_cd = 8;
        }
        else if (ly > 16000) {
          if (browser != Browser::Closed) browse_move(+1);
          else sel = (sel + 1) % item_count();
          axis_cd = 8;
        }
        else if (lx < -16000) { activate(sel, -1); axis_cd = 8; }
        else if (lx > 16000) { activate(sel, +1); axis_cd = 8; }
        else axis_cd = 0;
      }
      // frame: darkened game picture + panel
      for (size_t i = 0; i < cv.size(); ++i) {
        uint32_t c = bg[i];
        cv[i] = ((c >> 1) & 0x7F7F7F) | 0xFF000000u;
      }
      std::vector<std::string> items;
      const char* heading = "DEBUG MENU";
      switch (page) {
        case Page::Main:
          items = {"Resume", "Graphics >", "Controls >", "Save states >",
                   "Load states >", "Extras >", "Send ESC to game", "Quit"};
          break;
        case Page::Graphics:
          heading = "GRAPHICS";
          items = {"Back", "3D renderer: " + hires_label(),
                   std::string("Widescreen: ") + (widescreen ? "on" : "off"),
                   "Render FPS: " + std::to_string(render_fps) + " (OpenGL)",
                   std::string("Full detail: ") + (machine->full_detail ? "on" : "off"),
                   std::string("Stats overlay: ") + (stats_on ? "on" : "off"),
                   std::string("FPS counter: ") + (fps_on ? "on" : "off"),
                   std::string("Amiga blitter: ") +
                     (machine->original_blitter_writes() ? "on" : "OFF (debug)")};
          break;
        case Page::Controls:
          heading = "CONTROLS";
          items = {"Back", std::string("Analog stick: ") +
                             (machine->analog_enabled ? "on" : "off"), "Controller help >"};
          break;
        case Page::Extras:
          heading = "EXTRAS";
          items = {"Back", "Mesh viewer", std::string("Text pacing: ") +
                                             (machine->text_pace ? "on" : "off")};
          break;
      }
      int px = 8 * sc, py = 8 * sc;
      if (help_open) {
        for (size_t i = 0; i < control_help::pages[size_t(help_page)].size(); ++i) {
          menu_text(px, py, std::string(control_help::pages[size_t(help_page)][i]),
                    i == 0 ? 0xFFFFD040u : 0xFFDDDDDDu);
          py += control_help::row_height * sc;
        }
        menu_text(px, control_help::footer_y * sc,
                  "Left/Right page  A/Tab next  B/Esc back", 0xFF80A0FFu);
      } else if (browser != Browser::Closed) {
        menu_text(px, py, browser == Browser::Save ? "SAVE STATES" : "LOAD STATES", 0xFFFFD040u);
        py += 16 * sc;
        const int first = (browse_sel / browse_rows) * browse_rows;
        const int count = browse_count();
        for (int i = first; i < std::min(count, first + browse_rows); ++i) {
          std::string name;
          if (i == 0) name = "Back";
          else if (browser == Browser::Save && i == 1) name = "New Save";
          else {
            const int idx = i - (browser == Browser::Save ? 2 : 1);
            name = std::to_string(idx + 1) + ". " + saves[size_t(idx)].label;
          }
          if (name.size() > 42) name.resize(42);
          menu_text(px, py, std::string(i == browse_sel ? "> " : "  ") + name,
                    i == browse_sel ? 0xFFFFFFFFu : 0xFFB0B0B0u);
          py += 12 * sc;
        }
        menu_text(px, 164 * sc,
                  std::to_string(saves.size()) + " SAVES   " +
                    std::to_string(first + 1) + "-" + std::to_string(std::min(count, first + browse_rows)) +
                    "/" + std::to_string(count), 0xFFFFD040u);
        if (confirm_overwrite) {
          menu_text(px, 178 * sc, "Overwrite selected save?", 0xFFFF6060u);
          menu_text(px, 190 * sc, "A/Enter YES   B/Esc NO", 0xFF80A0FFu);
        } else {
          menu_text(px, 178 * sc, "A/Enter select   B/Esc back", 0xFF80A0FFu);
          menu_text(px, 190 * sc, "Up/Down browse   L/R page", 0xFF80A0FFu);
        }
      } else {
        menu_text(px, py, heading, 0xFFFFD040u);
        py += 16 * sc;
        if (getenv("FA18_MENU_TRACE") && getenv("FA18_MENU_SHOT")) {
          fprintf(stderr, "menu: %s", heading);
          for (const auto& line : items) fprintf(stderr, " | %s", line.c_str());
          fprintf(stderr, "\n");
        }
        for (int i = 0; i < int(items.size()); ++i) {
          std::string line = (i == sel ? "> " : "  ") + items[size_t(i)];
          menu_text(px, py, line, i == sel ? 0xFFFFFFFFu : 0xFFB0B0B0u);
          py += 12 * sc;
        }
        py += 3 * sc;
        menu_text(px, py, "A/Enter do  Up/Down select  L/R change", 0xFF80A0FFu);
        py += 13 * sc;
        if (menu_status.empty())
          menu_text(px, py, "Y/H controls help   B/Back/Esc close", 0xFF80A0FFu);
        else
          menu_text(px, py, menu_status, 0xFF60FF60u);
        if (page == Page::Graphics)
          menu_text(px, 153 * sc,
                    direct_gl ? "GL output: direct GPU (startup)" :
                                "GL output: SDL/CPU (startup)",
                    0xFFFFD040u);
      }
      if (w != tex_w || h != tex_h) {
        SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        tex_w = w;
        tex_h = h;
      }
      if (const char* shot = getenv("FA18_MENU_SHOT")) {  // debugging aid
        if (FILE* f = fopen(shot, "wb")) {
          fprintf(f, "P6\n%d %d\n255\n", w, h);
          for (size_t i = 0; i < cv.size(); ++i) {
            uint8_t px[3] = {uint8_t(cv[i] >> 16), uint8_t(cv[i] >> 8), uint8_t(cv[i])};
            fwrite(px, 1, 3, f);
          }
          fclose(f);
        }
        open = false;
      }
      bool gpu_menu = false;
      if (direct_gl && glr::ready() && !fast) {
        int dw = 0, dh = 0;
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        gpu_menu = glr::present_bitmap_gpu(cv.data(), w, h, amiga::FB_W, dw, dh);
        if (gpu_menu) SDL_GL_SwapWindow(win);
      }
      if (!gpu_menu) {
        SDL_UpdateTexture(tex, nullptr, cv.data(), w * 4);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
      }
      SDL_Delay(16);
    }
    if (audio_dev) SDL_PauseAudioDevice(audio_dev, 0);
    next_frame = SDL_GetPerformanceCounter();
    // buttons still held from the menu must not reach the game
    if (pad)
      for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
        pad_blocked[b] = SDL_GameControllerGetButton(pad, SDL_GameControllerButton(b)) != 0;
    pad_prev_btns = 0;
    std::fill(sjoy, sjoy + 5, false);
  }

  void frame(const uint32_t* fb) override { present(fb, amiga::FB_W, amiga::FB_H); }
  void frame_hires(const uint32_t* fb, int w, int h) override { present(fb, w, h); }
  int side_columns() const {
    if (!widescreen) return 0;
    int dw = 0, dh = 0;
    if (SDL_GetRendererOutputSize(ren, &dw, &dh) != 0 || dh <= 0) return 0;
    const int required = int(std::ceil(double(amiga::FB_H * 6 / 5) * dw / dh));
    // Equal left/right extension keeps every original screen coordinate fixed.
    return std::clamp((required - amiga::FB_W + 1) / 2, 0, 320);
  }
  void display_frame(const uint32_t* fb, int w, int h, int logical_w) {
    SDL_RenderSetLogicalSize(ren, logical_w, amiga::FB_H * 6 / 5);
    if (w != tex_w || h != tex_h) {
      SDL_DestroyTexture(tex);
      tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
      tex_w = w;
      tex_h = h;
    }
    SDL_UpdateTexture(tex, nullptr, fb, w * 4);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    if (stats_on || fps_on || !machine->original_blitter_writes()) draw_stats(logical_w);
    SDL_RenderPresent(ren);
    report_presentation();
  }
  void report_presentation() {
    // Fast-forward deliberately ignores the real-time presentation target;
    // never include it in the achieved-Hz diagnostic.
    if (fast) {
      present_report_ms = 0;
      present_report_count = 0;
      observed_fps = 0.f;
      extra_gl_missed = 0;
    } else {
      ++present_report_count;
      const uint32_t now = SDL_GetTicks();
      if (!present_report_ms) present_report_ms = now;
      const uint32_t elapsed = now - present_report_ms;
      if (elapsed >= 1000) {
        observed_fps = float(double(present_report_count) * 1000.0 / double(elapsed));
        if (render_fps > 60 && glr::active() && std::getenv("FA18_GL_PRESENT_DEBUG"))
          fprintf(stderr, "gl: observed %.1f presents/s (target %d), missed %llu extra deadlines\n",
                  observed_fps, render_fps, (unsigned long long)extra_gl_missed);
        present_report_count = 0;
        extra_gl_missed = 0;
        present_report_ms = now;
      }
    }
  }
  bool display_gpu(int side, float alpha) {
    if (gpu_layers_frame != frame_no) {
      if (!remaster::prepare_gpu_layers(*machine, gpu_layers)) return false;
      gpu_layers_frame = frame_no;
    }
    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(win, &dw, &dh);
    const bool show_hud = stats_on || fps_on || !machine->original_blitter_writes();
    const bool pause_visible = remaster::pause_screen_active();
    if (pause_visible) prepare_pause_ui();
    const uint8_t* new_hud = nullptr;
    if (show_hud && hud_frame != frame_no) {
      prepare_hud();
      hud_rgba.resize(stats_buf.size() * 4);
      for (size_t i = 0; i < stats_buf.size(); ++i) {
        const uint32_t argb = stats_buf[i];
        hud_rgba[i * 4 + 0] = uint8_t(argb >> 16);
        hud_rgba[i * 4 + 1] = uint8_t(argb >> 8);
        hud_rgba[i * 4 + 2] = uint8_t(argb);
        hud_rgba[i * 4 + 3] = uint8_t(argb >> 24);
      }
      hud_frame = frame_no;
      new_hud = hud_rgba.data();
      if (std::getenv("FA18_GL_DIRECT_DEBUG"))
        fprintf(stderr, "gl: native GPU HUD frame %d stats=%d fps=%d\n",
                frame_no, int(stats_on), int(fps_on));
    }
    if (!glr::present_gpu(gpu_layers, uint64_t(gpu_layers_frame),
                          side, machine->hires_scale, alpha, dw, dh,
                          new_hud, show_hud,
                          pause_visible ? pause_rgba.data() : nullptr, pause_visible,
                          remaster::cockpit_atlas(*machine).rgba.data()))
      return false;
    if (std::getenv("FA18_GL_DIRECT_COMPARE") && frame_no % 30 == 0) {
      // Diagnostic only: compare the GPU-owned window against the existing
      // CPU compositor at corresponding output sample centres. Normal GL
      // presentation has no readback; this mode deliberately does one.
      std::vector<uint8_t> actual;
      const bool read = glr::read_gpu_window(actual, dw, dh);
      const bool expected = remaster::compose_view(*machine, view_buf, side, alpha, true);
      if (read && expected) {
        blend_pause_ui(view_buf, side, machine->hires_scale);
        const int wide = amiga::FB_W + 2 * side;
        const int S = machine->hires_scale;
        const double aspect = double(wide) / 240.0;
        const int vw = std::min(dw, int(std::round(dh * aspect)));
        const int vh = std::min(dh, int(std::round(dw / aspect)));
        const int ox = (dw - vw) / 2, oy = (dh - vh) / 2;
        // Retain historical source-centre parity for the existing HUD tests.
        // A stricter output-subpixel oracle for thin native vectors is opt-in.
        const bool subpixel_parity = std::getenv("FA18_GL_DIRECT_SUBPIXEL_PARITY") != nullptr;
        int mismatches = 0, total = 0;
        for (int y = 0; y < 200; ++y) {
          const int sy = std::clamp(oy + int((y + 0.5) * vh / 200), 0, dh - 1);
          for (int x = 0; x < wide; ++x) {
            const int sx = std::clamp(ox + int((x + 0.5) * vw / wide), 0, dw - 1);
            const uint8_t* p = &actual[(size_t(dh - 1 - sy) * dw + sx) * 4];
            // The aspect-corrected window may have 720 rows while the CPU
            // vector layer has 600. Sample matching subpixels when requested;
            // otherwise use the original logical-cell midpoint reference.
            const int cx = subpixel_parity ?
              std::clamp(int((sx - ox + 0.5) * wide * S / vw), 0, wide * S - 1) :
              x * S + S / 2;
            const int cy = subpixel_parity ?
              std::clamp(int((sy - oy + 0.5) * 200 * S / vh), 0, 200 * S - 1) :
              y * S + S / 2;
            const uint32_t colour = view_buf[size_t(cy) * wide * S + cx];
            ++total;
            if (p[0] != uint8_t(colour >> 16) || p[1] != uint8_t(colour >> 8) ||
                p[2] != uint8_t(colour)) {
              if (mismatches < 3)
                fprintf(stderr, "gl: direct diff at %d,%d gpu=%02x%02x%02x cpu=%06x\n",
                        x, y, p[0], p[1], p[2], colour & 0xffffff);
              ++mismatches;
            }
          }
        }
        fprintf(stderr, "gl: direct parity frame %d different %d/%d (%.2f%%)\n",
                frame_no, mismatches, total, 100.0 * mismatches / total);
      }
    }
    SDL_GL_SwapWindow(win);
    if (std::getenv("FA18_GL_DIRECT_DEBUG")) {
      static int reports = 0;
      if (++reports == 1 || reports % 120 == 0)
        fprintf(stderr, "gl: direct GPU presentation %d\n", reports);
    }
    report_presentation();
    return true;
  }
  bool display_gpu_bitmap(const uint32_t* pixels, int w, int h, int logical_width) {
    if (!direct_gl || !glr::ready()) return false;
    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(win, &dw, &dh);
    const bool show_hud = stats_on || fps_on || !machine->original_blitter_writes();
    if (show_hud && hud_frame != frame_no) {
      prepare_hud();
      hud_rgba.resize(stats_buf.size() * 4);
      for (size_t i = 0; i < stats_buf.size(); ++i) {
        const uint32_t argb = stats_buf[i];
        hud_rgba[i * 4 + 0] = uint8_t(argb >> 16);
        hud_rgba[i * 4 + 1] = uint8_t(argb >> 8);
        hud_rgba[i * 4 + 2] = uint8_t(argb);
        hud_rgba[i * 4 + 3] = uint8_t(argb >> 24);
      }
      hud_frame = frame_no;
    }
    if (!glr::present_bitmap_gpu(pixels, w, h, logical_width, dw, dh,
                                 show_hud ? hud_rgba.data() : nullptr)) return false;
    if (std::getenv("FA18_GL_BITMAP_COMPARE") && !show_hud) {
      std::vector<uint8_t> actual;
      if (glr::read_gpu_window(actual, dw, dh)) {
        const double aspect = double(logical_width) / 240.0;
        const int vw = std::min(dw, int(std::round(dh * aspect)));
        const int vh = std::min(dh, int(std::round(dw / aspect)));
        const int ox = (dw - vw) / 2, oy = (dh - vh) / 2;
        int different = 0;
        for (int y = 0; y < h; ++y) {
          const int sy = std::clamp(oy + int((y + 0.5) * vh / h), 0, dh - 1);
          for (int x = 0; x < w; ++x) {
            const int sx = std::clamp(ox + int((x + 0.5) * vw / w), 0, dw - 1);
            const uint8_t* p = &actual[(size_t(dh - 1 - sy) * dw + sx) * 4];
            const uint32_t colour = pixels[size_t(y) * w + x];
            different += p[0] != uint8_t(colour >> 16) ||
                         p[1] != uint8_t(colour >> 8) || p[2] != uint8_t(colour);
          }
        }
        fprintf(stderr, "gl: bitmap parity frame %d different %d/%d\n",
                frame_no, different, w * h);
      }
    }
    SDL_GL_SwapWindow(win);
    report_presentation();
    return true;
  }
  void present(const uint32_t* fb, int w, int h) {
    ++frame_no;
    {
      std::string m = machine->take_state_message();
      if (!m.empty()) menu_status = m;
    }
    if (mesh_viewer_request && script.empty()) {
      mesh_viewer_request = false;
      run_mesh_viewer_standalone(fb, w, h);
    }
    if (menu_request && (script.empty() || getenv("FA18_MENU_SHOT"))) run_menu(fb, w, h);
    const bool gl_video = glr::active() && machine->hires_scale > 0 &&
                          (widescreen || render_fps > 60 || direct_gl);
    const int side = gl_video ? side_columns() : 0;
    if (gl_video && glr::capture_serial() != last_gl_serial) {
      last_gl_serial = glr::capture_serial();
      last_gl_tick = frame_no;
      gl_interval_ticks = std::max(1, glr::capture_interval_vblanks());
    }
    // The game's actual 3D redraw rate can be 7-15 Hz despite 60 Hz VBLs.
    // Hold one scene behind and advance the interpolation phase across the
    // measured number of VBLs between successive geometry captures.
    auto phase = [&](float subframe) {
      if (render_fps <= 60 || !glr::interpolation_available()) return 1.f;
      return std::clamp((float(frame_no - last_gl_tick) + subframe) /
                        float(gl_interval_ticks), 0.f, 1.f);
    };
    const uint32_t* video = fb;
    int vw = w, vh = h, logical_w = amiga::FB_W;
    // Keep SDL compositing for screenshots/stats and for contexts that cannot
    // present directly. Fast scripted runs normally avoid swapchain work,
    // but the explicit parity diagnostic must exercise the *real* GPU path
    // even under --fast; otherwise the headless parity test silently tests
    // SDL composition instead of the advertised direct renderer.
    const bool fast_gpu_diagnostic = std::getenv("FA18_GL_DIRECT_COMPARE") != nullptr;
    const bool try_gpu = direct_gl && gl_video && (!fast || fast_gpu_diagnostic) &&
                         frame_no != screenshot_frame && pending_shot.empty();
    const bool gpu_presented = try_gpu && display_gpu(side, phase(0.f));
    if (!gpu_presented && gl_video && remaster::compose_view(*machine, view_buf, side, phase(0.f),
                                           render_fps > 60)) {
      blend_pause_ui(view_buf, side, machine->hires_scale);
      video = view_buf.data();
      vw = (amiga::FB_W + 2 * side) * machine->hires_scale;
      vh = amiga::FB_H * machine->hires_scale;
      logical_w = amiga::FB_W + 2 * side;
    }
    if (gl_video && !gpu_presented && video != view_buf.data() &&
        std::getenv("FA18_GL_PRESENT_DEBUG"))
      fprintf(stderr, "gl: frame %d fell back to the original framebuffer\n", frame_no);
    // Keep the same GL swapchain even on menus, before the first complete
    // scene, or when the enhanced scene cannot be composited yet.  Otherwise
    // switching unpredictably between SDL_RenderPresent and SDL_GL_SwapWindow
    // can expose an undefined backbuffer on some drivers.
    const bool bitmap_gpu_presented = !gpu_presented && direct_gl &&
        (!fast || fast_gpu_diagnostic) &&
        frame_no != screenshot_frame && pending_shot.empty() &&
        display_gpu_bitmap(video, vw, vh, logical_w);
    if (!gpu_presented && !bitmap_gpu_presented)
      display_frame(video, vw, vh, logical_w);
    if (frame_no == screenshot_frame) save_screenshot(video);
    if (!pending_shot.empty()) {
      std::swap(pending_shot, screenshot_path);
      save_screenshot(video);
      if (stats_on) save_stats_screenshot(video, screenshot_path + ".stats.ppm");
      std::swap(pending_shot, screenshot_path);
      pending_shot.clear();
    }
    if (quit_after >= 0 && frame_no >= quit_after) quit = true;
    handle_events();
    if (quit) throw amiga::QuitRequested{};
    // One game/physics tick per 1/60 s. Extra GL redraws use only cached
    // geometry, interpolated between the two most recent simulated frames.
    uint64_t freq = SDL_GetPerformanceFrequency();
    const uint64_t tick_start = next_frame;
    next_frame += freq / 60;
    uint64_t now = SDL_GetPerformanceCounter();
    if (fast || int64_t(next_frame - now) < -int64_t(freq / 10)) next_frame = now;
    if (!fast && gl_video && render_fps > 60 &&
        (gpu_presented || video == view_buf.data())) {
      const int samples = render_fps / 60;
      for (int i = 1; i < samples; ++i) {
        const uint64_t target = tick_start + (freq / 60) * uint64_t(i) / uint64_t(samples);
        while (int64_t(target - SDL_GetPerformanceCounter()) > int64_t(freq / 500)) {
#if defined(__EMSCRIPTEN__)
          emscripten_sleep(1);
#else
          SDL_Delay(1);
#endif
        }
        if (SDL_GetPerformanceCounter() > target + freq / 120) {
          ++extra_gl_missed;
          continue;
        }
        if (gpu_presented) {
          if (!display_gpu(side, phase(float(i) / samples))) break;
        } else {
          if (!remaster::compose_view(*machine, view_buf, side,
                                      phase(float(i) / samples), true)) break;
          blend_pause_ui(view_buf, side, machine->hires_scale);
          if (!bitmap_gpu_presented ||
              !display_gpu_bitmap(view_buf.data(), vw, vh, logical_w))
            display_frame(view_buf.data(), vw, vh, logical_w);
        }
        ++extra_gl_presented;
        if (std::getenv("FA18_GL_INTERP_DEBUG") &&
            (extra_gl_presented == 1 || extra_gl_presented % 60 == 0))
          fprintf(stderr, "gl: extra presentation %llu at VBL %d\n",
                  (unsigned long long)extra_gl_presented, frame_no);
        handle_events();
        if (quit) throw amiga::QuitRequested{};
      }
    }
    // Signed arithmetic: the counter may pass next_frame between reads.
    while (!fast) {
      int64_t left = int64_t(next_frame - SDL_GetPerformanceCounter());
      if (left <= 0) break;
      int64_t left_us = left * 1000000 / int64_t(freq);
      if (left_us > 2000) {
#if defined(__EMSCRIPTEN__)
        // ASYNCIFY: yield to the browser so input/paint stay responsive.
        emscripten_sleep(uint32_t(left_us / 1000 - 1));
#else
        SDL_Delay(uint32_t(left_us / 1000 - 1));
#endif
      }
#if defined(__EMSCRIPTEN__)
      else {
        // Even when the frame is late, yield once so the tab does not freeze.
        emscripten_sleep(0);
        break;
      }
#endif
    }
#if defined(__EMSCRIPTEN__)
    if (fast) emscripten_sleep(0);
#endif
  }

  void run_script() {
    for (size_t i = 0; i < pending_up.size();) {
      if (pending_up[i].first <= frame_no) {
        machine->key_event(uint8_t(pending_up[i].second), false);
        pending_up.erase(pending_up.begin() + long(i));
      } else {
        ++i;
      }
    }
    while (script_pos < script.size() && script[script_pos].frame <= frame_no) {
      const ScriptEvent& e = script[script_pos++];
      if (e.cmd == "menu") {
        menu_request = true;
      } else if ((e.cmd == "savestate" || e.cmd == "loadstate") && !e.args.empty()) {
        std::string path = machine->save_dir + "/state" + e.args[0] + ".sav";
        if (e.cmd == "savestate") machine->request_save_state(path);
        else machine->request_load_state(path);
      } else if (e.cmd == "stats") {  // stats on | stats off
        stats_on = !e.args.empty() && e.args[0] == "on";
      } else if (e.cmd == "fps") {  // fps on | fps off
        fps_on = !e.args.empty() && e.args[0] == "on";
      } else if (e.cmd == "blitter" && !e.args.empty()) {  // blitter on | off
        if (e.args[0] == "on" || e.args[0] == "off")
          set_original_blitter_writes(e.args[0] == "on");
      } else if (e.cmd == "textpace") {  // textpace on | textpace off
        machine->text_pace = !e.args.empty() && e.args[0] == "on";
      } else if (e.cmd == "fast") {  // fast on | fast off (timing tests)
        fast = !e.args.empty() && e.args[0] == "on";
      } else if (e.cmd == "detail") {  // detail full | detail normal
        machine->full_detail = !e.args.empty() && e.args[0] == "full";
      } else if (e.cmd == "analog") {  // analog <x> <y> | analog off  (needs --analog)
        if (e.args.size() >= 2) {
          machine->analog_x = float(std::atof(e.args[0].c_str()));
          machine->analog_y = float(std::atof(e.args[1].c_str()));
          machine->analog_active = true;
        } else {
          machine->analog_active = false;
        }
      } else if (e.cmd == "key" && !e.args.empty()) {
        static const std::pair<const char*, const char*> alias[] = {
            {"lshift", "Left Shift"}, {"rshift", "Right Shift"}, {"ctrl", "Left Ctrl"},
            {"lalt", "Left Alt"}, {"ralt", "Right Alt"}, {"equals", "="}, {"minus", "-"},
            {"comma", ","}, {"period", "."}, {"enter", "Keypad Enter"}};
        std::string name = e.args[0];
        for (auto& [a, b] : alias) if (name == a) name = b;
        int rk = amiga_rawkey(SDL_GetScancodeFromName(name.c_str()));
        if (rk < 0) rk = int(std::strtol(name.c_str(), nullptr, 16));
        std::string mode = e.args.size() > 1 ? e.args[1] : "tap";
        if (mode == "down") machine->key_event(uint8_t(rk), true);
        else if (mode == "up") machine->key_event(uint8_t(rk), false);
        else { machine->key_event(uint8_t(rk), true); pending_up.push_back({frame_no + 4, rk}); }
      } else if (e.cmd == "joy") {
        std::string st = e.args.empty() ? "" : e.args[0];
        const char* names = "udlrf";
        for (int k = 0; k < 5; ++k) sjoy[k] = st.find(names[k]) != std::string::npos;
      } else if (e.cmd == "mouse" && e.args.size() >= 2) {
        machine->mouse_move(std::atoi(e.args[0].c_str()), std::atoi(e.args[1].c_str()));
      } else if ((e.cmd == "lmb" || e.cmd == "rmb") && !e.args.empty()) {
        machine->mouse_button(e.cmd == "lmb" ? 0 : 1, e.args[0] != "0");
      } else if (e.cmd == "poke" && e.args.size() >= 2) {
        // poke <hexaddr> <hexvalue> [b|w|l]: force a game variable (test
        // scenarios use it to reach states that are hard to play into)
        uint32_t a = uint32_t(std::strtoul(e.args[0].c_str(), nullptr, 16));
        uint32_t v = uint32_t(std::strtoul(e.args[1].c_str(), nullptr, 16));
        char sz = e.args.size() > 2 ? e.args[2][0] : 'b';
        if (sz == 'l') machine->w32(a, v);
        else if (sz == 'w') machine->w16(a, uint16_t(v));
        else machine->w8(a, uint8_t(v));
      } else if (e.cmd == "shot" && !e.args.empty()) {
        pending_shot = e.args[0];
      } else if (e.cmd == "quit") {
        quit = true;
      }
    }
  }

  std::string pending_shot;

  // Produce one native-resolution HUD image shared by the SDL and GL paths.
  // Its origin is the *window* top-left, not the old centred 4:3 viewport.
  void prepare_hud() {
    stats_buf.assign(size_t(STATS_W) * STATS_H, 0);
    auto lines = stats_on ? stats_overlay::build(*machine) :
                            std::vector<stats_overlay::Line>{};
    if (!machine->original_blitter_writes())
      lines.insert(lines.begin(), stats_overlay::Line{"AMIGA BLITTER OFF (Ctrl+B)",
                                                      0xFFFF6060u});
    if (lines.empty() && !fps_on) return;
    const int lh = font6x10::H, pad = 4;
    size_t cols = 0;
    for (const auto& l : lines) cols = std::max(cols, l.text.size());
    char fps_label[64] = {};
    if (fps_on) {
      std::snprintf(fps_label, sizeof(fps_label), "FPS: %.1f", observed_fps);
      cols = std::max(cols, std::strlen(fps_label));
    }
    const int pw = std::min(STATS_W, int(cols) * font6x10::W + 2 * pad);
    const int ph = std::min(STATS_H, int(lines.size() + int(fps_on)) * lh + 2 * pad);
    for (int y = 0; y < ph; ++y)
      for (int x = 0; x < pw; ++x) stats_buf[size_t(y) * STATS_W + size_t(x)] = 0xA0000000u;
    int y = pad;
    for (const auto& l : lines) {
      draw_text(stats_buf, STATS_W, STATS_H, pad, y, l.text, 1, l.color);
      y += lh;
    }
    if (fps_on) draw_text(stats_buf, STATS_W, STATS_H, pad, y, fps_label, 1, 0xFF60FF60u);
  }

  void draw_stats(int /*logical_w*/) {
    prepare_hud();
    if (!stats_tex) {
      stats_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                    STATS_W, STATS_H);
      if (!stats_tex) return;
      SDL_SetTextureBlendMode(stats_tex, SDL_BLENDMODE_BLEND);
    }
    SDL_UpdateTexture(stats_tex, nullptr, stats_buf.data(), STATS_W * 4);
    // On widescreen, the origin stays at the actual left edge, not at the
    // boundary where the old 4:3 pixels begin.
    SDL_Rect dst{0, 0, amiga::FB_W, amiga::FB_H * 6 / 5};
    SDL_RenderCopy(ren, stats_tex, nullptr, &dst);
  }

  // Script "shot" with the overlay on: write what the player actually sees.
  // HUD occupies the leftmost 320 columns even on a widened screenshot.
  void save_stats_screenshot(const uint32_t* fb, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f || stats_buf.empty()) {
      if (f) fclose(f);
      return;
    }
    fprintf(f, "P6\n%d %d\n255\n", tex_w, tex_h);
    const int centre_w = std::min(tex_w, amiga::FB_W * std::max(1, machine->hires_scale));
    const int left = 0;
    for (int y = 0; y < tex_h; ++y)
      for (int x = 0; x < tex_w; ++x) {
        const uint32_t g = fb[size_t(y) * size_t(tex_w) + size_t(x)];
        uint32_t o = 0;
        if (x >= left && x < left + centre_w) {
          const int sx = (x - left) * STATS_W / centre_w;
          const int sy = y * STATS_H / tex_h;
          o = stats_buf[size_t(sy) * STATS_W + size_t(sx)];
        }
        uint32_t a = o >> 24;
        uint8_t px[3];
        for (int c = 0; c < 3; ++c) {
          uint32_t gc = (g >> (16 - 8 * c)) & 0xFF, oc = (o >> (16 - 8 * c)) & 0xFF;
          px[c] = uint8_t((oc * a + gc * (255 - a)) / 255);
        }
        fwrite(px, 1, 3, f);
      }
    fclose(f);
  }

  void save_screenshot(const uint32_t* fb) {
    FILE* f = fopen(screenshot_path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", tex_w, tex_h);
    for (int i = 0; i < tex_w * tex_h; ++i) {
      uint8_t px[3] = {uint8_t(fb[i] >> 16), uint8_t(fb[i] >> 8), uint8_t(fb[i])};
      fwrite(px, 1, 3, f);
    }
    fclose(f);
  }

  FILE* wav = nullptr;
  uint32_t wav_bytes = 0;

  void open_wav(const std::string& path) {
    wav = fopen(path.c_str(), "wb");
    if (wav) { uint8_t hdr[44] = {}; fwrite(hdr, 1, 44, wav); }
  }

  void close_wav() {
    if (!wav) return;
    auto le32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) fputc(int(v >> (8 * i)) & 0xFF, wav); };
    auto le16 = [&](uint16_t v) { fputc(v & 0xFF, wav); fputc(v >> 8, wav); };
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav); le32(36 + wav_bytes); fwrite("WAVEfmt ", 1, 8, wav);
    le32(16); le16(1); le16(2); le32(48000); le32(48000 * 4); le16(4); le16(16);
    fwrite("data", 1, 4, wav); le32(wav_bytes);
    fclose(wav);
    wav = nullptr;
  }

  void audio(const int16_t* samples, size_t frames) override {
    if (wav) { fwrite(samples, 4, frames, wav); wav_bytes += uint32_t(frames * 4); }
    if (!audio_dev || fast) return;
    // Keep latency bounded.
    if (SDL_GetQueuedAudioSize(audio_dev) > 48000 * 4 / 5) return;
    SDL_QueueAudio(audio_dev, samples, uint32_t(frames * 4));
  }
};

void usage() {
  fprintf(stderr,
          "usage: fa18 [options] [data]\n"
          "  data             path to the original .adf disk image, or a\n"
          "                   directory of unpacked disk files (legacy)\n"
          "                   (default: looks for a nearby .adf or ./assets)\n"
          "\n"
          "Graphics (the original renderer is the default):\n"
          "  --hires N        software 3D upscaling (1-8x); no OpenGL\n"
          "  --gl N           OpenGL 3D scene renderer (1-8x)\n"
          "  --gl-direct      present the GL scene/HUD with the GPU, without readback\n"
          "                   implies --gl 3 if no 3D renderer was specified\n"
          "  --widescreen     extend OpenGL view horizontally (16:9; resizable)\n"
          "  --render-fps N   GL presentation: 60, 120, 180 or 240 Hz (not physics)\n"
          "  --full-detail    nearest level of detail for every 3D object\n"
          "  --stats          show game-world statistics\n"
          "  --fps            show observed presentation FPS\n"
          "  --scale N        initial window scale (default 3)\n"
          "                   --hires and --gl are mutually exclusive\n"
          "\n"
          "Gameplay and data:\n"
          "  --save DIR       directory for config/pilot log\n"
          "                   (default: $XDG_DATA_HOME/fa18-interceptor)\n"
          "  --speed X        CPU speed multiplier (default 4: smooth 15 fps;\n"
          "                   1 = stock A500 speed, 6-8 fps in flight)\n"
          "  --analog         remaster: analog flight stick (left stick)\n"
          "  --text-pace      remaster: menu texts and flight messages at stock A500 pace\n"
          "  --protection     keep the original codewheel check\n"
          "  --grab           grab mouse at startup (F12 toggles)\n"
          "  --no-grab        do not grab mouse (default)\n"
          "  --world FILE     remaster: use an edited world file\n"
          "\n"
          "Diagnostics and scripted replays:\n"
          "  --verbose        log OS calls\n"
          "  --trace          trace every instruction (very slow)\n"
          "  --screenshot N FILE  save frame N as PPM\n"
          "  --frames N       quit after N frames\n"
          "  --fast           run unthrottled (for automated replays)\n"
          "  --wav FILE       record audio\n"
          "  --interpret      use the 68000 interpreter instead of recompiled code\n"
          "  --native MODE    on (default): use hand-translated C++ routines;\n"
          "                   off: original code only; verify: run both and compare\n"
          "  --coverage FILE  write interpreted instruction coverage\n"
          "  --misses FILE    list program addresses that had to be interpreted\n"
          "  --script FILE    scripted input (see source)\n"
          "  --mesh-dump DIR  decode the object models, write one PPM per LOD to DIR\n"
          "  --world-dump FILE  write the world database (object maps, missions) as text\n"
          "  --map-dump FILE  top-down map (water, objects) of the world as PPM\n"
          "  --det-timing     deterministic timing for replay comparisons\n"
          "  --vbl-time       use video-blank timing\n"
          "  --ram-hash N     log emulated RAM hashes every N frames\n"
          "  --ram-dump N FILE  dump emulated RAM at frame N\n"
          "  --no-amiga-blitter suppress original Amiga blitter drawing (debug)\n"
          "\n"
          "keys: Ctrl+B toggle original Amiga blitter drawing,\n"
          "      Ctrl+F9/Insert debug menu, Ctrl+F10 mesh viewer,\n"
          "      Ctrl+F11 original/enhanced graphics toggle,\n"
          "      F12 grab/release mouse (port 1), F11 fullscreen,\n"
          "      Pause fast-forward.  Game controller (see README):\n"
          "      stick/d-pad fly, A fire, B weapon, X target, Y map,\n"
          "      LT/RT throttle, LB systems pie, RB views pie, Start pause.\n"
          "      Keyboard: cursor fly, F1-F10/=/- throttle, SPACE fire,\n"
          "      RETURN weapon, numpad views (full list in the manual).\n");
}

// OpenGL renderer of the 3D view (--gl): normally a hidden context with SDL
// presentation; --gl-direct uses a second context on the visible SDL window.
bool init_gl(SDL_Window* visible) {
  // Prefer GLES 2 on ports built with FA18_OPENGLES2 (Android, Emscripten,
  // R36S) or when FA18_GLES=1; otherwise try desktop GL 2.1 first.
#if defined(__EMSCRIPTEN__)
  // The WASM build already owns the single canvas via SDL_Renderer (WebGL).
  // A second SDL_WINDOW_OPENGL context aborts the module; the software
  // hi-res path is the supported 3D upscale on the web.
  fprintf(stderr, "gl: not available on the web build (single canvas); using software hi-res\n");
  return false;
#endif
  SDL_Window* w = visible;
  SDL_GLContext ctx = nullptr;
  std::string err;
#if defined(FA18_OPENGLES2) || defined(__ANDROID__)
  const bool prefer_es = true;
#else
  const bool prefer_es = std::getenv("FA18_GLES") != nullptr;
#endif
  for (int pass = 0; pass < 2 && !ctx; ++pass) {
    const bool es = prefer_es ? (pass == 0) : (pass == 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, es ? SDL_GL_CONTEXT_PROFILE_ES : 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, es ? 0 : 1);
    if (!w) w = SDL_CreateWindow("fa18 gl", 0, 0, 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (w) ctx = SDL_GL_CreateContext(w);
    if (!ctx) err = SDL_GetError();
  }
  if (ctx && glr::init([](const char* n) { return SDL_GL_GetProcAddress(n); },
                       [w, ctx] { SDL_GL_MakeCurrent(w, ctx); }, err)) {
    if (visible) SDL_GL_SetSwapInterval(std::getenv("FA18_GL_VSYNC") ? 1 : 0);
    return true;
  }
  fprintf(stderr, "gl: not available (%s); using the software hi-res renderer\n", err.c_str());
  return false;
}

}  // namespace

#if defined(__EMSCRIPTEN__)
// HTML toolbar → these keep-alive entry points (see mk/wasm/shell.html).
static SdlHost* g_host_for_js = nullptr;
extern "C" {
EMSCRIPTEN_KEEPALIVE void fa18_request_menu() {
  if (g_host_for_js) g_host_for_js->menu_request = true;
}
EMSCRIPTEN_KEEPALIVE void fa18_request_mesh_viewer() {
  if (g_host_for_js) g_host_for_js->mesh_viewer_request = true;
}
}
#endif

int main(int argc, char** argv) {
  std::string data_dir, save_dir;
  int scale = 3;
  double speed = 4.0;  // game caps itself at 15 fps; 1.0 = stock A500
  bool verbose = false, trace = false, fast = false, protection = false, analog = false;
  bool full_detail = false, stats = false, fps = false, text_pace = false, widescreen = false;
  bool disable_amiga_blitter = false;
  bool gl_direct = false;
  int render_fps = 60;
  int hires = 0, gl_scale = 0;
  bool requested_hires = false, requested_gl = false;
  int shot_frame = -1, quit_after = -1;
  std::string shot_path, script_path, wav_path, coverage_path, misses_path;
  bool interpret = false, vbl_time = false, det_timing = false;
  int ram_hash = 0, ram_dump_frame = 0;
  std::string ram_dump_path, mesh_dump_dir, world_dump_path, world_path, map_dump_path;
  std::string native_mode = "on";
  bool grab_mouse = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--scale" && i + 1 < argc) scale = std::atoi(argv[++i]);
    else if (a == "--save" && i + 1 < argc) save_dir = argv[++i];
    else if (a == "--speed" && i + 1 < argc) speed = std::atof(argv[++i]);
    else if (a == "--verbose") verbose = true;
    else if (a == "--trace") trace = true;
    else if (a == "--screenshot" && i + 2 < argc) { shot_frame = std::atoi(argv[++i]); shot_path = argv[++i]; }
    else if (a == "--frames" && i + 1 < argc) quit_after = std::atoi(argv[++i]);
    else if (a == "--fast") fast = true;
    else if (a == "--analog") analog = true;
    else if (a == "--full-detail") full_detail = true;
    else if (a == "--no-amiga-blitter") disable_amiga_blitter = true;
    else if (a == "--text-pace") text_pace = true;
    else if (a == "--stats") stats = true;
    else if (a == "--fps") fps = true;
    else if (a == "--hires" && i + 1 < argc) {
      requested_hires = true;
      hires = std::atoi(argv[++i]);
    }
    else if (a == "--gl" && i + 1 < argc) {
      requested_gl = true;
      gl_scale = hires = std::atoi(argv[++i]);
    }
    else if (a == "--gl-direct") gl_direct = true;
    else if (a == "--widescreen") widescreen = true;
    else if (a == "--render-fps" && i + 1 < argc) render_fps = std::atoi(argv[++i]);
    else if (a == "--protection") protection = true;
    else if (a == "--wav" && i + 1 < argc) wav_path = argv[++i];
    else if (a == "--coverage" && i + 1 < argc) coverage_path = argv[++i];
    else if (a == "--interpret") interpret = true;
    else if (a == "--vbl-time") vbl_time = true;
    else if (a == "--det-timing") det_timing = true;
    else if (a == "--ram-hash" && i + 1 < argc) ram_hash = std::atoi(argv[++i]);
    else if (a == "--ram-dump" && i + 2 < argc) { ram_dump_frame = std::atoi(argv[++i]); ram_dump_path = argv[++i]; }
    else if (a == "--native" && i + 1 < argc) native_mode = argv[++i];
    else if (a == "--misses" && i + 1 < argc) misses_path = argv[++i];
    else if (a == "--script" && i + 1 < argc) script_path = argv[++i];
    else if (a == "--mesh-dump" && i + 1 < argc) mesh_dump_dir = argv[++i];
    else if (a == "--world-dump" && i + 1 < argc) world_dump_path = argv[++i];
    else if (a == "--world" && i + 1 < argc) world_path = argv[++i];
    else if (a == "--map-dump" && i + 1 < argc) map_dump_path = argv[++i];
    else if (a == "--grab") grab_mouse = true;
    else if (a == "--no-grab") grab_mouse = false;
    else if (a == "-h" || a == "--help") { usage(); return 0; }
    else if (!a.empty() && a[0] == '-') {
      fprintf(stderr, "Unknown or incomplete option: %s\n", a.c_str());
      usage();
      return 2;
    }
    else data_dir = a;
  }
  if (const char* direct = std::getenv("FA18_GL_DIRECT");
      direct && std::strcmp(direct, "0") != 0) gl_direct = true;
  if (requested_hires && requested_gl) {
    fprintf(stderr, "--hires and --gl select different 3D renderers; choose one\n");
    return 2;
  }
  if (gl_direct && requested_hires) {
    fprintf(stderr, "--gl-direct requires OpenGL; use --gl N, not --hires N\n");
    return 2;
  }
  if (gl_direct && requested_gl && gl_scale <= 0) {
    fprintf(stderr, "--gl-direct requires a positive --gl resolution\n");
    return 2;
  }
  if (gl_direct && !requested_gl) {
    // --gl-direct is a user-facing renderer choice, not merely a context
    // creation hint. Keep the historical 3x enhanced-graphics default.
    gl_scale = hires = 3;
  }
  if (render_fps < 60 || render_fps > 240 || render_fps % 60) {
    fprintf(stderr, "--render-fps must be 60, 120, 180 or 240\n");
    return 2;
  }
  if (gl_scale == 0 && (widescreen || render_fps > 60))
    fprintf(stderr, "note: widescreen and high render FPS require OpenGL (--gl N or --gl-direct)\n");
  if (data_dir.empty()) {
    namespace fs = std::filesystem;
    // Prefer an ADF in the current directory / common names, then a legacy
    // unpacked assets/ directory.  On Android and Emscripten also look in the
    // platform's persistent storage (user-supplied ADF; never redistributed).
    const char* adf_names[] = {
      "FA-18 Interceptor (1988)(Electronic Arts).adf",
      "FA-18 Interceptor.adf",
      "fa18.adf",
      "F-18 Interceptor.adf",
      "disk.adf",
    };
    auto try_names_in = [&](const fs::path& dir) {
      std::error_code ec;
      if (dir.empty() || !fs::is_directory(dir, ec)) return false;
      for (const char* n : adf_names) {
        fs::path p = dir / n;
        if (fs::is_regular_file(p, ec)) { data_dir = p.string(); return true; }
      }
      return false;
    };
    for (const char* n : adf_names) {
      if (fs::is_regular_file(n)) { data_dir = n; break; }
    }
#if defined(__ANDROID__)
    if (data_dir.empty()) {
      // adb push … /data/data/io.github.grumbel.fa18/files/disk.adf
      if (const char* internal = SDL_AndroidGetInternalStoragePath())
        try_names_in(internal);
      if (data_dir.empty())
        if (const char* external = SDL_AndroidGetExternalStoragePath())
          try_names_in(external);
    }
#elif defined(__EMSCRIPTEN__)
    if (data_dir.empty()) {
      // shell.html stores the upload here after the IDBFS mount.
      try_names_in("/home/web_user/.local/share/fa18");
    }
#endif
    if (data_dir.empty()) {
      for (const char* cand : {"assets", "data", FA18_DATADIR}) {
        if (fs::exists(std::string(cand) + "/F-18 Interceptor")) { data_dir = cand; break; }
      }
    }
  }
  if (save_dir.empty()) {
#if defined(__ANDROID__)
    if (const char* internal = SDL_AndroidGetInternalStoragePath())
      save_dir = std::string(internal) + "/save";
    else
      save_dir = ".";
#elif defined(__EMSCRIPTEN__)
    save_dir = "/home/web_user/.local/share/fa18/save";
#else
    const char* xdg = getenv("XDG_DATA_HOME");
    const char* home = getenv("HOME");
    if (xdg && *xdg) save_dir = std::string(xdg) + "/fa18-interceptor";
    else if (home) save_dir = std::string(home) + "/.local/share/fa18-interceptor";
    else save_dir = ".";
#endif
  }
  {
    std::error_code ec;
    std::filesystem::create_directories(save_dir, ec);
  }
  if (data_dir.empty()) {
    fprintf(stderr,
            "Cannot find the game data.\n"
            "Pass the path to the original ADF (or an unpacked assets directory).\n"
#if defined(__ANDROID__)
            "On Android: adb push the ADF into the app files dir as disk.adf.\n"
#elif defined(__EMSCRIPTEN__)
            "On the web build: use the Upload ADF button on the page.\n"
#endif
            );
#if defined(__EMSCRIPTEN__)
    // Keep the runtime alive so the user can upload an ADF and reload.
    fprintf(stderr, "fa18: waiting for ADF upload (page stays open)\n");
    emscripten_exit_with_live_runtime();
#endif
    usage();
    return 1;
  }

  amiga::Machine machine;
  machine.verbose = verbose;
  machine.trace = trace;
  if (!det_timing) machine.set_cpu_speed(speed);
  machine.save_dir = save_dir;
  machine.bypass_protection = !protection;
  machine.analog_enabled = analog;
  machine.full_detail = full_detail;
  machine.text_pace = text_pace;
  machine.set_original_blitter_writes(!disable_amiga_blitter);
  machine.set_hires_scale(std::max(0, std::min(8, hires)));
  machine.coverage_path = coverage_path;
  machine.use_recomp = !interpret;
  machine.vbl_time = vbl_time;
  if (det_timing) machine.set_det_timing(true);
  machine.ram_hash_every = ram_hash;
  machine.ram_dump_frame = uint64_t(ram_dump_frame);
  machine.ram_dump_path = ram_dump_path;
  machine.native_mode = native_mode == "off"      ? amiga::Machine::NativeMode::Off
                        : native_mode == "verify" ? amiga::Machine::NativeMode::Verify
                                                  : amiga::Machine::NativeMode::On;
  if (!machine.boot(data_dir)) return 1;
  native::install_control_hooks(machine);
  if (!mesh_dump_dir.empty()) {
    std::filesystem::create_directories(mesh_dump_dir);
    std::vector<uint8_t> exe = machine.disk_read("F-18 Interceptor");
    return exe.empty() || !mesh_viewer::dump_models(exe, mesh_dump_dir) ? 1 : 0;
  }
  if (!world_path.empty()) {
    world::World w;
    std::string err;
    if (!world::load_file(world_path, w, err) || !world::apply(machine, w, err)) {
      fprintf(stderr, "world: %s\n", err.c_str());
      return 1;
    }
    fprintf(stderr, "world: using %s\n", world_path.c_str());
  }
  if (!world_dump_path.empty()) {
    world::World w;
    std::string err;
    // From memory, so a dump after --world shows the applied world.
    if (!world::decode([&](uint32_t at) { return machine.r8(at); }, w, err)) {
      fprintf(stderr, "world: %s\n", err.c_str());
      return 1;
    }
    std::ofstream(world_dump_path) << world::to_text(w);
    return 0;
  }
  if (!map_dump_path.empty()) {
    auto rd = [&](uint32_t at) { return machine.r8(at); };
    world::World w;
    std::string err;
    terrain::Terrain t;
    if (!world::decode(rd, w, err) || !terrain::decode(rd, t) ||
        !terrain::write_map(t, &w, map_dump_path)) {
      fprintf(stderr, "map: %s\n", err.empty() ? "cannot write" : err.c_str());
      return 1;
    }
    return 0;
  }

  fprintf(stderr, "%s\n", machine.recomp_active() ? "running recompiled code"
                                                   : "running in interpreter mode");
  if (disable_amiga_blitter)
    fprintf(stderr, "graphics: original Amiga blitter writes OFF (--no-amiga-blitter)\n");
  if (!misses_path.empty()) machine.track_recomp_misses(true);

  SdlHost host;
#if defined(__EMSCRIPTEN__)
  g_host_for_js = &host;
#endif
  host.machine = &machine;
  host.screenshot_frame = shot_frame;
  host.screenshot_path = shot_path;
  host.quit_after = quit_after;
  host.fast = fast;
  host.widescreen = widescreen;
  host.direct_gl = gl_direct;
  host.render_fps = render_fps;
  glr::set_deferred_scene_readback(widescreen || render_fps > 60 || host.direct_gl);
  host.stats_on = stats;
  host.fps_on = fps;
  if (!script_path.empty()) host.script = load_script(script_path);
  if (!host.init(scale)) return 1;
  if (gl_scale > 0 && !init_gl(host.direct_gl ? host.win : nullptr))
    gl_failed = true;  // software hi-res renderer
  if (glr::active())
    fprintf(stderr, "graphics: OpenGL 3D %dx, %s output\n", machine.hires_scale,
            host.direct_gl ? "direct GPU" : "SDL/CPU");
  else if (machine.hires_scale > 0)
    fprintf(stderr, "graphics: software 3D %dx, SDL output%s\n", machine.hires_scale,
            gl_failed ? " (OpenGL unavailable)" : "");
  else
    fprintf(stderr, "graphics: original 320x200, SDL output\n");
  if (grab_mouse) host.set_grab(true);
  machine.set_host(&host);
  if (!wav_path.empty()) host.open_wav(wav_path);
  int rc = machine.run();
  host.close_wav();
  if (native_mode == "verify" || machine.verbose) machine.print_native_stats();
  if (!misses_path.empty() && machine.recomp_misses()) {
    FILE* f = fopen(misses_path.c_str(), "w");
    if (f) {
      for (uint32_t a : *machine.recomp_misses()) fprintf(f, "%06x\n", a);
      fclose(f);
    }
  }
  SDL_Quit();
  return rc;
}
