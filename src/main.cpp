#include <SDL.h>
#include <iostream>
#include <string>
#include <algorithm>
#include "amiga_hunk.hpp"

int main(int argc, char* argv[]) {
  std::cout << "FA-18 Interceptor - modern port (skeleton + analysis)\n\n";

  std::string exe_path = "assets/F-18_Interceptor_amiga";
  if (argc > 1) exe_path = argv[1];

  AmigaExecutable exe;
  if (!exe.load(exe_path)) {
    std::cerr << "Failed to load " << exe_path << "\n";
    return 1;
  }
  exe.dump_summary();

  // Print a curated set of interesting strings
  auto strs = exe.extract_strings(6);
  std::cout << "\nInteresting strings (sample):\n";
  const char* keywords[] = {
    "INTERCEPTOR", "MISSION", "QUALIF", "CARRIER", "MISSILE",
    "library", "device", "ENLISTED", "BOB", "DAVE", "MOSES",
    "ELECTRONIC", "FALCON", "HORNET", "LANDING", "FUEL"
  };
  int shown = 0;
  for (const auto& s : strs) {
    for (const char* kw : keywords) {
      if (s.find(kw) != std::string::npos) {
        std::cout << "  \"" << s << "\"\n";
        if (++shown >= 25) goto done_strings;
        break;
      }
    }
  }
done_strings:
  std::cout << "  ... (" << strs.size() << " total strings >= 6 chars)\n";

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  SDL_Window* win = SDL_CreateWindow(
      "FA-18 Interceptor (port)",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      640, 400, SDL_WINDOW_SHOWN);
  if (!win) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 1;
  }

  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  if (!ren) {
    std::cerr << "SDL_CreateRenderer failed\n";
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
    // Classic Amiga Workbench-ish blue
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
