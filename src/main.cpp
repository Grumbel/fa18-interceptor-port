// F/A-18 Interceptor — SDL2 front end.
#include "amiga/machine.hpp"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace {

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
  // keyboard joystick (numpad)
  bool kup = false, kdown = false, kleft = false, kright = false, kfire = false;

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
      if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }
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
          if (sc == SDL_SCANCODE_F11 && down) {
            Uint32 fl = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
            SDL_SetWindowFullscreen(win, fl ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            break;
          }
          if (sc == SDL_SCANCODE_PAUSE && down) { fast = !fast; break; }
          if (sc == SDL_SCANCODE_F4 && down && (e.key.keysym.mod & KMOD_ALT)) { quit = true; break; }
          // Numpad doubles as joystick on port 2 (8/2/4/6, 0 or 5 = fire).
          switch (sc) {
            case SDL_SCANCODE_KP_8: kup = down; break;
            case SDL_SCANCODE_KP_2: kdown = down; break;
            case SDL_SCANCODE_KP_4: kleft = down; break;
            case SDL_SCANCODE_KP_6: kright = down; break;
            case SDL_SCANCODE_KP_0: case SDL_SCANCODE_KP_5: case SDL_SCANCODE_RCTRL:
              kfire = down; break;
            default: break;
          }
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
          if (!mouse_grab && down && e.button.button == SDL_BUTTON_LEFT) { set_grab(true); break; }
          if (e.button.button == SDL_BUTTON_LEFT) machine->mouse_button(0, down);
          if (e.button.button == SDL_BUTTON_RIGHT) machine->mouse_button(1, down);
          break;
        }
        case SDL_CONTROLLERDEVICEADDED:
          if (!pad) pad = SDL_GameControllerOpen(e.cdevice.which);
          break;
      }
    }
    bool up = kup, down = kdown, left = kleft, right = kright, fire = kfire;
    if (pad) {
      int x = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
      int y = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
      up |= y < -12000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
      down |= y > 12000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
      left |= x < -12000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
      right |= x > 12000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
      fire |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) != 0;
    }
    machine->joy_state(up, down, left, right, fire);
  }

  void frame(const uint32_t* fb) override {
    ++frame_no;
    SDL_UpdateTexture(tex, nullptr, fb, amiga::FB_W * 4);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);
    if (frame_no == screenshot_frame) save_screenshot(fb);
    if (quit_after >= 0 && frame_no >= quit_after) quit = true;
    handle_events();
    if (quit) throw amiga::QuitRequested{};
    // Pace to 60 Hz unless fast-forwarding.
    uint64_t freq = SDL_GetPerformanceFrequency();
    next_frame += freq / 60;
    uint64_t now = SDL_GetPerformanceCounter();
    if (fast || int64_t(next_frame - now) < -int64_t(freq / 10)) next_frame = now;
    while (!fast && SDL_GetPerformanceCounter() < next_frame) {
      uint64_t left_us = (next_frame - SDL_GetPerformanceCounter()) * 1000000 / freq;
      if (left_us > 2000) SDL_Delay(uint32_t(left_us / 1000 - 1));
    }
  }

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

  void audio(const int16_t* samples, size_t frames) override {
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
          "  --save DIR       directory for config/pilot log (default: data-dir)\n"
          "  --speed X        CPU speed multiplier (default 1.0)\n"
          "  --verbose        log OS calls\n"
          "  --trace          trace every instruction (very slow)\n"
          "  --screenshot N FILE  save frame N as PPM\n"
          "  --frames N       quit after N frames\n"
          "  --fast           run unthrottled\n"
          "keys: F12 grab mouse, F11 fullscreen, Pause fast-forward,\n"
          "      numpad 8/2/4/6 + 0 = joystick\n");
}

}  // namespace

int main(int argc, char** argv) {
  std::string data_dir, save_dir;
  int scale = 3;
  double speed = 1.0;
  bool verbose = false, trace = false, fast = false;
  int shot_frame = -1, quit_after = -1;
  std::string shot_path;
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
    else if (a == "-h" || a == "--help") { usage(); return 0; }
    else data_dir = a;
  }
  if (data_dir.empty()) {
    for (const char* cand : {"assets", "data", FA18_DATADIR}) {
      if (std::filesystem::exists(std::string(cand) + "/F-18 Interceptor")) { data_dir = cand; break; }
    }
  }
  if (data_dir.empty()) {
    fprintf(stderr, "Cannot find the game data (F-18 Interceptor executable).\n");
    usage();
    return 1;
  }

  amiga::Machine machine;
  machine.verbose = verbose;
  machine.trace = trace;
  machine.cpu_speed = speed;
  machine.save_dir = save_dir;
  if (!machine.boot(data_dir)) return 1;

  SdlHost host;
  host.machine = &machine;
  host.screenshot_frame = shot_frame;
  host.screenshot_path = shot_path;
  host.quit_after = quit_after;
  host.fast = fast;
  if (!host.init(scale)) return 1;
  machine.set_host(&host);
  int rc = machine.run();
  SDL_Quit();
  return rc;
}
