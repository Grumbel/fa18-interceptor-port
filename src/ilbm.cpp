#include "ilbm.hpp"
#include <fstream>
#include <iostream>
#include <cstring>

static uint32_t rd_be32(const uint8_t* p) {
  return (p[0]<<24) | (p[1]<<16) | (p[2]<<8) | p[3];
}
static uint16_t rd_be16(const uint8_t* p) {
  return (p[0]<<8) | p[1];
}

// Decode ByteRun1 into dst; returns bytes consumed from src
static size_t byterun1_decode(const uint8_t* src, size_t src_len,
                              uint8_t* dst, size_t dst_len) {
  size_t si = 0, di = 0;
  while (si < src_len && di < dst_len) {
    int8_t n = static_cast<int8_t>(src[si++]);
    if (n >= 0) {
      // copy n+1 bytes
      int count = n + 1;
      for (int i = 0; i < count && si < src_len && di < dst_len; i++)
        dst[di++] = src[si++];
    } else if (n != -128) {
      // run of (-n)+1 copies of next byte
      int count = (-n) + 1;
      if (si >= src_len) break;
      uint8_t b = src[si++];
      for (int i = 0; i < count && di < dst_len; i++)
        dst[di++] = b;
    }
  }
  return si;
}

bool IlbmImage::load(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
  if (data.size() < 12) return false;
  if (std::memcmp(data.data(), "FORM", 4) != 0) return false;
  if (std::memcmp(data.data()+8, "ILBM", 4) != 0) return false;

  size_t pos = 12;
  int compression = 0;
  int masking = 0;
  std::vector<uint8_t> body;
  palette.clear();

  while (pos + 8 <= data.size()) {
    char tag[5] = {0};
    std::memcpy(tag, data.data()+pos, 4);
    uint32_t sz = rd_be32(data.data()+pos+4);
    pos += 8;
    if (pos + sz > data.size()) break;

    if (std::strcmp(tag, "BMHD") == 0 && sz >= 20) {
      width  = rd_be16(data.data()+pos+0);
      height = rd_be16(data.data()+pos+2);
      planes = data[pos+8];
      masking = data[pos+9];
      compression = data[pos+10];
    } else if (std::strcmp(tag, "CMAP") == 0) {
      for (uint32_t i = 0; i + 2 < sz; i += 3)
        palette.push_back({data[pos+i], data[pos+i+1], data[pos+i+2], 255});
    } else if (std::strcmp(tag, "BODY") == 0) {
      body.assign(data.begin()+pos, data.begin()+pos+sz);
    }
    pos += sz + (sz & 1);
  }

  if (width <= 0 || height <= 0 || planes <= 0 || body.empty())
    return false;

  // Decode planar BODY into indexed pixels
  int row_words = (width + 15) / 16;
  int row_bytes = row_words * 2;
  int plane_size = row_bytes * height;
  std::vector<uint8_t> planar(plane_size * planes, 0);

  if (compression == 1) {
    byterun1_decode(body.data(), body.size(), planar.data(), planar.size());
  } else {
    std::memcpy(planar.data(), body.data(),
                std::min(body.size(), planar.size()));
  }

  pixels.assign(width * height, 0);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      uint8_t idx = 0;
      int bit = 15 - (x & 15);
      int word = x / 16;
      for (int p = 0; p < planes; p++) {
        const uint8_t* row = planar.data() + p * plane_size + y * row_bytes;
        uint16_t w = (row[word*2] << 8) | row[word*2+1];
        if (w & (1 << bit))
          idx |= (1 << p);
      }
      pixels[y * width + x] = idx;
    }
  }

  if (palette.empty()) {
    // grayscale fallback
    for (int i = 0; i < (1 << planes); i++) {
      uint8_t v = i * 255 / ((1 << planes) - 1);
      palette.push_back({v, v, v, 255});
    }
  }
  return true;
}

SDL_Texture* IlbmImage::to_texture(SDL_Renderer* ren) const {
  if (pixels.empty()) return nullptr;
  SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(
      0, width, height, 8, SDL_PIXELFORMAT_INDEX8);
  if (!surf) return nullptr;

  SDL_Palette* pal = SDL_AllocPalette(256);
  std::vector<SDL_Color> cols(256, {0,0,0,255});
  for (size_t i = 0; i < palette.size() && i < 256; i++)
    cols[i] = palette[i];
  SDL_SetPaletteColors(pal, cols.data(), 0, 256);
  SDL_SetSurfacePalette(surf, pal);

  std::memcpy(surf->pixels, pixels.data(), pixels.size());
  SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
  SDL_FreeSurface(surf);
  SDL_FreePalette(pal);
  return tex;
}
