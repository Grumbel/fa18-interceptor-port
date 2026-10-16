// Minimal AmigaDOS OFS/FFS ADF reader (mirrors tools/adf.py).
// Used at runtime so the game can load assets directly from the original
// disk image without unpacking into a directory first.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace amiga {

class Adf {
 public:
  static constexpr size_t BS = 512;

  bool load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    img_.assign(std::istreambuf_iterator<char>(in), {});
    if (img_.size() < BS * 2) return false;
    if (img_[0] != 'D' || img_[1] != 'O' || img_[2] != 'S') return false;
    ffs_ = (img_[3] & 1) != 0;
    root_ = int(img_.size() / BS / 2);
    // Build a case-folded path → header-block index for O(1) lookup.
    index_.clear();
    walk(root_, "");
    return true;
  }

  bool has(const std::string& amiga_path) const {
    return index_.count(fold(amiga_path)) != 0;
  }

  // Return file contents, or empty if missing / is a directory.
  std::vector<uint8_t> read(const std::string& amiga_path) const {
    auto it = index_.find(fold(amiga_path));
    if (it == index_.end() || it->second.size < 0) return {};
    return read_file(it->second.hdr);
  }

  // File size in bytes, or -1 if missing / directory.
  int32_t size(const std::string& amiga_path) const {
    auto it = index_.find(fold(amiga_path));
    if (it == index_.end()) return -1;
    return it->second.size;
  }

  bool is_dir(const std::string& amiga_path) const {
    auto it = index_.find(fold(amiga_path));
    return it != index_.end() && it->second.size < 0;
  }

 private:
  struct Entry {
    int hdr = 0;
    int32_t size = 0;  // -1 for directories
  };

  std::vector<uint8_t> img_;
  bool ffs_ = false;
  int root_ = 0;
  std::map<std::string, Entry> index_;

  static std::string fold(std::string s) {
    // Normalise separators and strip a leading volume (df0: etc.).
    auto colon = s.find(':');
    if (colon != std::string::npos) s = s.substr(colon + 1);
    while (!s.empty() && s.front() == '/') s.erase(s.begin());
    for (char& c : s) {
      if (c == '\\') c = '/';
      c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    // Collapse duplicate slashes.
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '/' && !out.empty() && out.back() == '/') continue;
      out.push_back(s[i]);
    }
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
  }

  const uint8_t* blk(int n) const {
    size_t off = size_t(n) * BS;
    if (off + BS > img_.size()) return nullptr;
    return img_.data() + off;
  }

  static int32_t l(const uint8_t* b, int i) {
    const uint8_t* p = b + i * 4;
    return int32_t(uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 |
                   uint32_t(p[2]) << 8 | p[3]);
  }

  static std::string name(const uint8_t* b) {
    int n = b[BS - 80];
    if (n < 0 || n > 30) n = 0;
    return std::string(reinterpret_cast<const char*>(b + BS - 79), size_t(n));
  }

  void walk(int dir_block, const std::string& prefix) {
    const uint8_t* b = blk(dir_block);
    if (!b) return;
    for (int i = 6; i < 6 + 72; ++i) {
      int h = l(b, i);
      while (h) {
        const uint8_t* e = blk(h);
        if (!e) break;
        int sec = l(e, int(BS / 4) - 1);
        std::string path = prefix + name(e);
        if (sec == 2) {  // ST_USERDIR
          index_[fold(path)] = {h, -1};
          walk(h, path + "/");
        } else {
          index_[fold(path)] = {h, l(e, int(BS / 4) - 47)};
        }
        h = l(e, int(BS / 4) - 4);  // hash chain
      }
    }
  }

  std::vector<uint8_t> read_file(int hdr) const {
    const uint8_t* b = blk(hdr);
    if (!b) return {};
    int32_t size = l(b, int(BS / 4) - 47);
    if (size < 0) return {};
    std::vector<uint8_t> out;
    out.reserve(size_t(size));
    if (ffs_) {
      // data block list from header + extension blocks
      const uint8_t* cur = b;
      std::vector<int> blocks;
      while (cur) {
        int n = l(cur, 2);
        for (int k = 0; k < n; ++k) blocks.push_back(l(cur, 6 + 71 - k));
        int ext = l(cur, int(BS / 4) - 2);
        cur = ext ? blk(ext) : nullptr;
      }
      for (int p : blocks) {
        const uint8_t* db = blk(p);
        if (db) out.insert(out.end(), db, db + BS);
      }
    } else {
      int d = l(b, 4);  // first data block
      while (d) {
        const uint8_t* db = blk(d);
        if (!db) break;
        int n = l(db, 3);
        if (n < 0 || n > int(BS - 24)) n = 0;
        out.insert(out.end(), db + 24, db + 24 + n);
        d = l(db, 4);
      }
    }
    if (int32_t(out.size()) > size) out.resize(size_t(size));
    return out;
  }
};

}  // namespace amiga
