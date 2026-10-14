// F/A-18 Interceptor — SDL2 front end.
#include "amiga/machine.hpp"

#include <SDL.h>

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
  int pad_prev_hat = -1;      // layered d-pad/stick selection (-1 none, 0-7 dirs, 8 center)
  int pad_throttle_cd = 0;    // frames until next throttle step
  bool pad_held_key[128] = {};

  bool init(int scale) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
      fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
      return false;
    }
    win = SDL_CreateWindow("F/A-18 Interceptor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           amiga::FB_W * scale, amiga::FB_H * scale * 6 / 5,
                           SDL_WINDOW_RESIZABLE);
    if (!win) return false;
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    // NTSC lores pixels are slightly taller than wide; 320x240 approximates.
    SDL_RenderSetLogicalSize(ren, amiga::FB_W, amiga::FB_H * 6 / 5);
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

  void handle_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_QUIT: quit = true; break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
          bool down = e.type == SDL_KEYDOWN;
          SDL_Scancode sc = e.key.keysym.scancode;
          if (sc == SDL_SCANCODE_F12 && down) { set_grab(!mouse_grab); break; }
          if (sc == SDL_SCANCODE_F9 && down) {
            machine->analog_enabled = !machine->analog_enabled;
            std::printf("analog stick: %s\n", machine->analog_enabled ? "on (remaster)" : "off (original)");
            break;
          }
          if (sc == SDL_SCANCODE_F11 && down) {
            Uint32 fl = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
            SDL_SetWindowFullscreen(win, fl ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
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
      return SDL_GameControllerGetButton(pad, b) != 0;
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
      if (pressed & (1u << 5)) pad_tap(0x45);       // Back = Esc
      if (pressed & (1u << 6)) pad_tap(0x41);       // LS click = brakes
      if (pressed & (1u << 7)) pad_tap(0x24);       // RS click = gear
    }

    // Hold layers act as pie menus: aim with stick/d-pad, release the
    // shoulder to commit.  Face buttons are shortcuts while the layer is held.
    // LB wins over RB if both are down.
    if (released & (1u << 8)) {
      // N=R, NE=J, E=T, SE=C, S=M, SW=F, W=H, NW=A, center=G
      static const int sys_keys[9] = {
        0x13, 0x26, 0x14, 0x33, 0x37, 0x23, 0x25, 0x20, 0x24
      };
      if (pad_prev_hat >= 0 && pad_prev_hat <= 8) pad_tap(sys_keys[pad_prev_hat]);
      pad_prev_hat = -1;
    } else if (lb) {
      pad_prev_hat = pad_dir8(lx, ly, du, dd, dl, dr);
      if (pressed & (1u << 0)) pad_tap(0x25);  // A = H (HUD)
      if (pressed & (1u << 1)) pad_tap(0x26);  // B = J (ECM)
      if (pressed & (1u << 2)) pad_tap(0x33);  // X = C (chaff)
      if (pressed & (1u << 3)) pad_tap(0x23);  // Y = F (flare)
    } else if (released & (1u << 9)) {
      // Keypad 8,9,6,3,2,1,4,7,5
      static const int view_keys[9] = {
        0x3E, 0x3F, 0x2F, 0x1F, 0x1E, 0x1D, 0x2D, 0x3D, 0x2E
      };
      if (pad_prev_hat >= 0 && pad_prev_hat <= 8) pad_tap(view_keys[pad_prev_hat]);
      pad_prev_hat = -1;
    } else if (rb) {
      pad_prev_hat = pad_dir8(lx, ly, du, dd, dl, dr);
      if (pressed & (1u << 0)) pad_tap(0x43);  // A = keypad Enter (outside view)
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

  void frame(const uint32_t* fb) override {
    ++frame_no;
    SDL_UpdateTexture(tex, nullptr, fb, amiga::FB_W * 4);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);
    if (frame_no == screenshot_frame) save_screenshot(fb);
    if (!pending_shot.empty()) {
      std::swap(pending_shot, screenshot_path);
      save_screenshot(fb);
      std::swap(pending_shot, screenshot_path);
      pending_shot.clear();
    }
    if (quit_after >= 0 && frame_no >= quit_after) quit = true;
    handle_events();
    if (quit) throw amiga::QuitRequested{};
    // Pace to 60 Hz unless fast-forwarding.
    uint64_t freq = SDL_GetPerformanceFrequency();
    next_frame += freq / 60;
    uint64_t now = SDL_GetPerformanceCounter();
    if (fast || int64_t(next_frame - now) < -int64_t(freq / 10)) next_frame = now;
    // Signed arithmetic: the counter may pass next_frame between reads.
    while (!fast) {
      int64_t left = int64_t(next_frame - SDL_GetPerformanceCounter());
      if (left <= 0) break;
      int64_t left_us = left * 1000000 / int64_t(freq);
      if (left_us > 2000) SDL_Delay(uint32_t(left_us / 1000 - 1));
    }
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
      if (e.cmd == "analog") {  // analog <x> <y> | analog off  (needs --analog)
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

  void save_screenshot(const uint32_t* fb) {
    FILE* f = fopen(screenshot_path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", amiga::FB_W, amiga::FB_H);
    for (int i = 0; i < amiga::FB_W * amiga::FB_H; ++i) {
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
          "usage: fa18 [options] [data-dir]\n"
          "  data-dir         directory with the files of the original disk\n"
          "                   (default: ./assets or the install data dir)\n"
          "  --scale N        window scale (default 3)\n"
          "  --save DIR       directory for config/pilot log\n"
          "                   (default: $XDG_DATA_HOME/fa18-interceptor)\n"
          "  --speed X        CPU speed multiplier (default 4: smooth 15 fps;\n"
          "                   1 = stock A500 speed, 6-8 fps in flight)\n"
          "  --verbose        log OS calls\n"
          "  --trace          trace every instruction (very slow)\n"
          "  --screenshot N FILE  save frame N as PPM\n"
          "  --frames N       quit after N frames\n"
          "  --fast           run unthrottled\n"
          "  --analog         remaster: analog flight stick (left stick); F9 toggles\n"
          "  --protection     keep the original codewheel check\n"
          "  --wav FILE       record audio\n"
          "  --interpret      use the 68000 interpreter instead of recompiled code\n"
          "  --native MODE    on (default): use hand-translated C++ routines;\n"
          "                   off: original code only; verify: run both and compare\n"
          "  --misses FILE    list program addresses that had to be interpreted\n"
          "  --script FILE    scripted input (see source)\n"
          "  --grab           capture the mouse at start (off by default; F12 toggles)\n"
          "keys: F12 grab/release mouse (port 1), F11 fullscreen,\n"
          "      Pause fast-forward.  Game controller (see README):\n"
          "      stick/d-pad fly, A fire, B weapon, X target, Y map,\n"
          "      LT/RT throttle, LB systems pie, RB views pie, Start pause.\n"
          "      Keyboard: cursor fly, F1-F10/=/- throttle, SPACE fire,\n"
          "      RETURN weapon, numpad views (full list in the manual).\n");
}

}  // namespace

int main(int argc, char** argv) {
  std::string data_dir, save_dir;
  int scale = 3;
  double speed = 4.0;  // game caps itself at 15 fps; 1.0 = stock A500
  bool verbose = false, trace = false, fast = false, protection = false, analog = false;
  int shot_frame = -1, quit_after = -1;
  std::string shot_path, script_path, wav_path, coverage_path, misses_path;
  bool interpret = false, vbl_time = false, det_timing = false;
  int ram_hash = 0, ram_dump_frame = 0;
  std::string ram_dump_path;
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
    else if (a == "--grab") grab_mouse = true;
    else if (a == "--no-grab") grab_mouse = false;
    else if (a == "-h" || a == "--help") { usage(); return 0; }
    else data_dir = a;
  }
  if (data_dir.empty()) {
    for (const char* cand : {"assets", "data", FA18_DATADIR}) {
      if (std::filesystem::exists(std::string(cand) + "/F-18 Interceptor")) { data_dir = cand; break; }
    }
  }
  if (save_dir.empty()) {
    const char* xdg = getenv("XDG_DATA_HOME");
    const char* home = getenv("HOME");
    if (xdg && *xdg) save_dir = std::string(xdg) + "/fa18-interceptor";
    else if (home) save_dir = std::string(home) + "/.local/share/fa18-interceptor";
    else save_dir = ".";
  }
  std::filesystem::create_directories(save_dir);
  if (data_dir.empty()) {
    fprintf(stderr, "Cannot find the game data (F-18 Interceptor executable).\n");
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
  fprintf(stderr, "%s\n", machine.recomp_active() ? "running recompiled code"
                                                   : "running in interpreter mode");
  if (!misses_path.empty()) machine.track_recomp_misses(true);

  SdlHost host;
  host.machine = &machine;
  host.screenshot_frame = shot_frame;
  host.screenshot_path = shot_path;
  host.quit_after = quit_after;
  host.fast = fast;
  if (!script_path.empty()) host.script = load_script(script_path);
  if (!host.init(scale)) return 1;
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
