#pragma once
#include <SDL.h>
#include <string>
#include <vector>
#include <cstdint>

// Minimal IFF ILBM loader (BMHD + CMAP + BODY, ByteRun1 or uncompressed).
// Sufficient for the small on-disk assets (frnt5, inst5).

struct IlbmImage {
  int width = 0;
  int height = 0;
  int planes = 0;
  std::vector<uint8_t> pixels;   // width*height, indexed
  std::vector<SDL_Color> palette; // up to 32 entries

  bool load(const std::string& path);
  SDL_Texture* to_texture(SDL_Renderer* ren) const;
};
