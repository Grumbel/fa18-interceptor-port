#include <SDL.h>
#include <iostream>
#include <string>
#include "amiga_hunk.hpp"
#include "game_state.hpp"

int main(int argc, char* argv[]) {
  std::cout << "FA-18 Interceptor - modern port (skeleton)\n\n";

  std::string exe_path = "assets/F-18_Interceptor_amiga";
  if (argc > 1) exe_path = argv[1];

  AmigaExecutable exe;
  if (!exe.load(exe_path)) {
    std::cerr << "Failed to load " << exe_path << "\n";
    return 1;
  }
  exe.dump_summary();

  // Demonstrate the high-level state machine recovered so far
  Game game;
  for (int i = 0; i < 7; ++i)
    game.request_next();

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  SDL_Window* win = SDL_CreateWindow(
      "FA-18 Interceptor (port)",
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

  bool running = true;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
        running = false;
    }
    SDL_SetRenderDrawColor(ren, 0x00, 0x55, 0xaa, 255);
    SDL_RenderClear(ren);
    SDL_RenderPresent(ren);
    SDL_Delay(16);
  }

  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
