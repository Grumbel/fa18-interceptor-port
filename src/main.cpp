#include <SDL.h>
#include <iostream>
#include <string>
#include <sstream>
#include "amiga_hunk.hpp"
#include "game_state.hpp"
#include "ilbm.hpp"

int main(int argc, char* argv[]) {
  std::cout << "FA-18 Interceptor - modern port (skeleton)\n"
            << "Keys: SPACE/ENTER = next state, BACKSPACE = prev, ESC = quit\n\n";

  std::string exe_path = "assets/F-18_Interceptor_amiga";
  if (argc > 1) exe_path = argv[1];

  AmigaExecutable exe;
  if (!exe.load(exe_path)) {
    std::cerr << "Failed to load " << exe_path << "\n";
    return 1;
  }
  exe.dump_summary();
  std::cout << "\n";

  Game game;

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  SDL_Window* win = SDL_CreateWindow(
      "FA-18 Interceptor (port) - Startup",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      640, 400, SDL_WINDOW_SHOWN);
  if (!win) {
    std::cerr << "SDL_CreateWindow failed\n";
    SDL_Quit();
    return 1;
  }
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  if (!ren) {
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // nearest-neighbour

  IlbmImage frnt5, inst5;
  SDL_Texture* tex_frnt = nullptr;
  SDL_Texture* tex_inst = nullptr;
  if (frnt5.load("assets/pix/frnt5")) {
    tex_frnt = frnt5.to_texture(ren);
    std::cout << "Loaded frnt5: " << frnt5.width << "x" << frnt5.height
              << " " << frnt5.planes << " planes\n";
  }
  if (inst5.load("assets/pix/inst5")) {
    tex_inst = inst5.to_texture(ren);
    std::cout << "Loaded inst5: " << inst5.width << "x" << inst5.height
              << " " << inst5.planes << " planes\n";
  }

  auto colour_for = [](GameState s) -> SDL_Color {
    switch (s) {
      case GameState::Startup:       return {0x00, 0x20, 0x40, 255};
      case GameState::Title:          return {0x00, 0x55, 0xaa, 255};
      case GameState::Enlistment:     return {0x20, 0x60, 0x20, 255};
      case GameState::Qualification:  return {0x60, 0x40, 0x00, 255};
      case GameState::MissionSelect:  return {0x40, 0x00, 0x60, 255};
      case GameState::Flight:         return {0x00, 0x40, 0x80, 255};
      case GameState::FlightLog:      return {0x40, 0x40, 0x40, 255};
      case GameState::Exit:           return {0x20, 0x00, 0x00, 255};
    }
    return {0, 0, 0, 255};
  };

  auto update_title = [&]() {
    std::ostringstream oss;
    oss << "FA-18 Interceptor (port) - " << game.state_name();
    SDL_SetWindowTitle(win, oss.str().c_str());
  };
  update_title();

  bool running = true;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.sym) {
          case SDLK_ESCAPE: running = false; break;
          case SDLK_SPACE:
          case SDLK_RETURN:
            game.request_next();
            update_title();
            if (game.state() == GameState::Exit) running = false;
            break;
          case SDLK_BACKSPACE:
            game.request_prev();
            update_title();
            break;
          default: break;
        }
      }
    }

    SDL_Color c = colour_for(game.state());
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
    SDL_RenderClear(ren);

    // Scale ILBM assets 2x with nearest-neighbour for a more visible placeholder
    if (game.state() == GameState::Title && tex_frnt) {
      int w = frnt5.width * 2, h = frnt5.height * 2;
      SDL_Rect dst = { (640-w)/2, (400-h)/2, w, h };
      SDL_RenderCopy(ren, tex_frnt, nullptr, &dst);
    } else if ((game.state() == GameState::Flight ||
                game.state() == GameState::Qualification) && tex_inst) {
      int w = inst5.width * 2, h = inst5.height * 2;
      SDL_Rect dst = { (640-w)/2, 400-h-8, w, h };
      SDL_RenderCopy(ren, tex_inst, nullptr, &dst);
    }

    SDL_RenderPresent(ren);
    SDL_Delay(16);
  }

  if (tex_frnt) SDL_DestroyTexture(tex_frnt);
  if (tex_inst) SDL_DestroyTexture(tex_inst);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
