#include "mesh_viewer.hpp"

#include "amiga/hunk.hpp"
#include "native/mesh_emu.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

namespace mesh_viewer {
namespace {

using namespace emu;
using namespace emu::asmr;

constexpr uint32_t TYPE_TABLE = 0x36094;  // 20 bytes per type
constexpr uint32_t TEMPLATES = 0x35f54;   // 16 scenario-object types, copied
constexpr int NUM_TEMPLATES = 16;         // into slot k for scenario record k
constexpr uint32_t SCENARIO = 0x5b47c;    // 16 records of 512 bytes
constexpr int TEMPLATE_SLOT = 1;          // type slot / record used for them
constexpr uint32_t JUMP_TABLE = 0x33478;  // polygon ops, byte offsets $00..$134
constexpr uint32_t JUMP_TABLE_END = 0x135;
constexpr uint32_t POOL = 0x5d684;        // transformed vertex pool
constexpr uint32_t POLY_BUF = 0x61284;    // $61286 count, $61288 vertices
constexpr uint32_t LINE_BUF = 0x61886;
constexpr uint32_t MEM_SIZE = 0x70000;
constexpr uint32_t FRAME = 0x6f000;       // a6 of the draw routine frame
constexpr uint32_t STACK = 0x6ff00;
constexpr int MAX_TYPES = 160;
// The pool holds view-space words.  The decoder works in object space with
// the camera on the -z side: depth offset so the ops' "behind the camera" and
// frustum tests pass, and a per-model shift so all words stay in range.
constexpr int Z_OFFSET = 12000;
constexpr int MAX_COORD = 8000;

bool is_draw_fn(uint32_t fn) {
  switch (fn) {
    case 0x31c8c: case 0x31c98: case 0x31c9c: case 0x31d64: case 0x32d64: case 0x32d72:
    case 0x369cc: case 0x36a26: case 0x36ac6:  // scenario objects, then $31c9c
      return true;
    default:
      return false;
  }
}
bool is_distant_fn(uint32_t fn) { return fn == 0x32d64 || fn == 0x32d72; }

// Relocated image at the runtime addresses (same layout as Machine::boot).
bool relocate(const std::vector<uint8_t>& exe, std::vector<uint8_t>& img) {
  amiga::HunkFile hf;
  if (!hf.parse(exe)) return false;
  std::vector<uint32_t> addrs;
  uint32_t a = amiga::LOAD_BASE;
  for (auto& h : hf.hunks) {
    addrs.push_back(a + 8);
    a += 8 + h.alloc_size;
  }
  if (a > STACK - 0x1000) return false;
  img.assign(MEM_SIZE, 0);
  auto put32 = [&](uint32_t at, uint32_t v) {
    img[at] = uint8_t(v >> 24);
    img[at + 1] = uint8_t(v >> 16);
    img[at + 2] = uint8_t(v >> 8);
    img[at + 3] = uint8_t(v);
  };
  auto get32 = [&](uint32_t at) {
    return uint32_t(img[at]) << 24 | uint32_t(img[at + 1]) << 16 | uint32_t(img[at + 2]) << 8 |
           img[at + 3];
  };
  for (size_t i = 0; i < hf.hunks.size(); ++i) {
    const auto& h = hf.hunks[i];
    std::memcpy(&img[addrs[i]], h.data.data(), h.data.size());
    for (const auto& [target, offs] : h.relocs) {
      if (target >= addrs.size()) return false;
      for (uint32_t o : offs) put32(addrs[i] + o, get32(addrs[i] + o) + addrs[target]);
    }
  }
  return true;
}

int16_t s16(uint32_t v) { return int16_t(uint16_t(v)); }

class Decoder {
 public:
  explicit Decoder(const std::vector<uint8_t>& img) : img_(img) {
    env_.on_call = [this](uint32_t addr) { on_call(addr); };
  }

  // entry: 20-byte type record (draw routine, model base, LOD list ...);
  // slot: its type index ($5acac), also the scenario record for templates.
  Lod decode(uint32_t entry, int slot, int distance) {
    Lod lod;
    lod.distance = distance;
    lod_ = &lod;
    capture_ = false;
    op_addr_ = 0;
    shadows_.clear();
    env_.mem = img_;
    set_env(&env_);
    try {
      run(entry, slot, distance);
    } catch (const DecodeError& e) {
      lod.notes.push_back(e.what());
    }
    set_env(nullptr);
    lod_ = nullptr;
    return lod;
  }

  // LOD distances worth decoding: 0 and every threshold of the LOD chain.
  std::vector<int> lod_distances(uint32_t e) {
    env_.mem = img_;
    set_env(&env_);
    std::set<int> out{0};
    try {
      uint32_t base = rd32(e + 4), p = rd32(e + 8);
      bool distant = is_distant_fn(rd32(e));
      for (int guard = 0; guard < 32; ++guard) {
        uint16_t w = rd16(p);
        if (w & 0x8000) break;
        if (distant) {
          out.insert(int(w) + 1);
          uint32_t nxt = rd32(p + 2);
          if (int32_t(nxt) < 0) break;
          p = nxt;
          continue;
        }
        out.insert(int(w & 0x3fff));
        if (w & 0x4000) {
          p += 6;
          continue;
        }
        int16_t x = s16(rd16(p + 2));
        if (x < 0) break;
        p = base + uint32_t(int32_t(x));
      }
    } catch (const DecodeError&) {
    }
    set_env(nullptr);
    return {out.begin(), out.end()};
  }

 private:
  const std::vector<uint8_t>& img_;
  Env env_;
  Lod* lod_ = nullptr;
  bool capture_ = false;
  uint32_t op_addr_ = 0;                // handler being run
  std::vector<size_t> shadows_;         // prims from $33936 (ground shadow)
  int shift_ = 0;
  uint32_t base_ = 0, mesh_ = 0;
  uint8_t flags_ = 0;
  bool flat_ = false;

  void note(const std::string& s) {
    if (!capture_ || !lod_) return;
    if (std::find(lod_->notes.begin(), lod_->notes.end(), s) == lod_->notes.end())
      lod_->notes.push_back(s);
  }

  // object space -> pool words and back
  void put_vertex(uint32_t at, int x, int y, int z) {
    wr16(at, uint32_t(asr16(uint16_t(x), uint32_t(shift_))));
    wr16(at + 2, uint32_t(asr16(uint16_t(y), uint32_t(shift_))));
    wr16(at + 4, uint32_t(asr16(uint16_t(z), uint32_t(shift_)) + Z_OFFSET));
  }
  Vec3 obj(uint32_t at) {
    Vec3 v;
    v.x = int32_t(s16(rd16(at))) << shift_;
    v.y = int32_t(s16(rd16(at + 2))) << shift_;
    v.z = (int32_t(s16(rd16(at + 4))) - Z_OFFSET) << shift_;
    return v;
  }

  void emit(Prim::Kind kind, std::vector<Vec3> v, bool line) {
    if (!capture_) return;
    Prim p;
    p.kind = kind;
    p.v = std::move(v);
    uint16_t col = rd16(0x5ac4c);
    uint16_t mask = rd16(0x5a9de) & 15;
    int16_t w1 = s16(rd16(0x5a9e0));
    uint16_t w2 = rd16(0x5a9e2);
    if (w1 >= 0 && !line && rd16(0x5a9e4) != 0) {
      p.colour = uint8_t(col & 15);               // plane copy ($45014): tint only
      p.mask = 15;
    } else {
      // as remaster::poly_ops / line_ops
      p.mask = uint8_t(mask);
      if (!line) p.xr = uint8_t(w1 < 0 ? ((w2 & 1) ? mask : 0) : (w2 & mask));
      p.colour = uint8_t((w1 < 0 ? col : uint16_t(w1)) & mask & ~p.xr);
    }
    if (op_addr_ == 0x33936) shadows_.push_back(lod_->prims.size());
    lod_->prims.push_back(std::move(p));
  }

  void on_call(uint32_t addr) {
    switch (addr) {
      case 0x38c0a: case 0x38c0c: {               // polygon rasteriser
        int n = s16(rd16(POLY_BUF + 2));
        if (n < 2 || n > 40) { note("bad polygon size"); break; }
        std::vector<Vec3> v;
        for (int i = 0; i < n; ++i) v.push_back(obj(POLY_BUF + 4 + 6u * uint32_t(i)));
        emit(Prim::Poly, std::move(v), false);
        break;
      }
      case 0x43a56: case 0x4397c:                 // line
        emit(Prim::Line, {obj(LINE_BUF), obj(LINE_BUF + 6)}, true);
        break;
      case 0x4389c: case 0x438a0: case 0x438a8: case 0x438ac: case 0x438b0: case 0x438b4: {
        Vec3 v;                                   // marker at (d0,d1,d2)
        v.x = int32_t(s16(D(0))) << shift_;
        v.y = int32_t(s16(D(1))) << shift_;
        v.z = (int32_t(s16(D(2))) - Z_OFFSET) << shift_;
        emit(Prim::Point, {v}, true);
        D(0) = 1;
        break;
      }
      case 0x32ad2:                               // facing test: always front
        // $32adc reads a plane offset from the stream for colour bits 12/13
        if (!(lo(7) & 0x0c00) && (lo(7) & 0x3000)) A(2) += 2;
        setw(7, 0);
        D(0) = 0;
        flags(0, 16);
        break;
      default:                                    // hypot, divisions, lights...
        break;
    }
  }

  // Run the polygon op at jump-table offset w; false: stop this list.
  bool op(uint16_t w, uint32_t& p) {
    uint32_t off = w & 0x3fff;
    if (off >= JUMP_TABLE_END || (off & 3)) {
      note("op word $" + hex(w));
      return false;
    }
    uint32_t addr = rd32(JUMP_TABLE + off);
    OpFn fn = op_at(addr);
    if (!fn) {
      note("op $" + hex(addr));
      return false;
    }
    A(2) = p;
    A(6) = FRAME;
    A(7) = STACK;
    op_addr_ = addr;
    if (addr == 0x335f8) note("animation state of op $335f8 unknown (state 0)");
    fn();
    op_addr_ = 0;
    p = A(2);
    return !(cpu().fn != cpu().fv);  // blt: the list is abandoned
  }

  static std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "%x", v);
    return b;
  }

  void frame_setup(int distance) {
    for (uint32_t a = FRAME - 160; a < FRAME; a += 2) wr16(a, 0);
    wr16(FRAME - 40, uint32_t(distance));         // distance limit
    wr32(FRAME - 44, base_);                      // model base
    wr16(FRAME - 8, 0);                           // vertex shift
    wr16(FRAME - 6, 0);
  }

  // ---- vertex transforms (s328ea / s32f6e and the header block) --------------
  void xform_full(int n, uint16_t off) {          // $328ea: n vertices at offset off
    uint32_t src = mesh_ + 10 + uint32_t(int32_t(s16(off)));
    if (!flat_) {
      uint32_t dst = POOL + uint32_t(int32_t(s16(off)));
      for (int k = 0; k < n; ++k, src += 6, dst += 6)
        put_vertex(dst, s16(rd16(src)), s16(rd16(src + 2)), s16(rd16(src + 4)));
      return;
    }
    int16_t o = s16(off);
    uint32_t dst = POOL + uint32_t(int32_t(int16_t(o + (o >> 1))));
    for (int k = 0; k < n; ++k, src += 4, dst += 6) put_vertex(dst, s16(rd16(src)), 0, s16(rd16(src + 2)));
  }
  void xform_distant(int n, uint16_t off) {       // $32f6e
    int16_t o = s16(off);
    uint32_t src = mesh_ + 6 + uint32_t(int32_t(o));
    uint32_t dst = POOL + uint32_t(int32_t(int16_t(o + (o >> 1))));
    for (int k = 0; k < n; ++k, src += 4, dst += 6) put_vertex(dst, s16(rd16(src)), 0, s16(rd16(src + 2)));
  }

  // $324da: extra 3D vertex runs (header flag bit 3), interpolated points
  void extra_runs_3d(uint32_t a1, uint32_t a3) {
    int32_t acc[6] = {};
    auto read_vertex = [&](int32_t* l) {
      uint32_t at = a3;
      put_vertex(at, s16(rd16(a1)), s16(rd16(a1 + 2)), s16(rd16(a1 + 4)));
      a1 += 6;
      for (int i = 0; i < 3; ++i) l[i] = int32_t(s16(rd16(at + 2u * uint32_t(i)))) * 256;
    };
    auto store = [&](const int32_t* l, int sh) {
      for (int i = 0; i < 3; ++i) wr16(a3 + 2u * uint32_t(i), uint32_t(l[i] >> sh));
    };
    for (int guard = 0; guard < 64; ++guard) {
      int16_t n = s16(rd16(a1));                  // $324da
      a1 += 2;
      if (n < 0) return;
      int32_t stride = 12;
      read_vertex(acc);                           // $324e8: written at a3, a3 += 12
      a3 += 12;
      uint32_t save = a3;
      for (int g2 = 0; g2 < 256; ++g2) {
        int32_t q[3];
        uint32_t keep = a3;
        read_vertex(q);                           // $32546 (transform only)
        a3 = keep;
        int16_t c = s16(rd16(a1));
        a1 += 2;
        if (c > 1) {
          int32_t p[3], d[3];
          for (int i = 0; i < 3; ++i) {
            p[i] = acc[i] >> 4;
            int32_t num = (q[i] >> 4) - p[i];
            int32_t qt = num / c;
            d[i] = (qt >= -32768 && qt <= 32767) ? int32_t(int16_t(qt)) : int32_t(int16_t(num));
          }
          for (int k = 0; k < c - 1; ++k) {
            for (int i = 0; i < 3; ++i) p[i] += d[i];
            store(p, 4);
            a3 += uint32_t(stride);
          }
        }
        for (int i = 0; i < 3; ++i) acc[i] = q[i];
        store(acc, 8);
        a3 += uint32_t(stride);
        if (--n > 0) continue;
        if (stride < 0) break;
        stride = -12;
        save = a3;
        uint16_t w = rd16(a1);
        a1 += 2;
        if (w == 0) break;
        if (w & 0x8000) {
          a3 -= 6;
          w &= 0x7fff;
        } else {
          a3 -= 18;
        }
        n = int16_t(w);
      }
      if (s16(rd16(a1)) <= 0) return;             // $3263c
      a3 = save;
    }
  }

  // $32c3e: flat extrusions (header flag bit 2): each vertex also at +delta
  void extra_runs_flat(uint32_t a1, uint32_t a3) {
    for (int guard = 0; guard < 64; ++guard) {
      int16_t n = s16(rd16(a1));
      a1 += 2;
      if (n < 0) return;
      int16_t d[3];
      for (int i = 0; i < 3; ++i)
        d[i] = int16_t(rd16(a3 - 6 + 2u * uint32_t(i)) - rd16(a3 - 12 + 2u * uint32_t(i)));
      do {
        put_vertex(a3, s16(rd16(a1)), 0, s16(rd16(a1 + 2)));
        a1 += 4;
        for (int i = 0; i < 3; ++i)
          wr16(a3 + 6 + 2u * uint32_t(i), uint32_t(rd16(a3 + 2u * uint32_t(i)) + uint16_t(d[i])));
        a3 += 12;
      } while (--n > 0);
      int16_t t = s16(rd16(a1));
      a1 += 2;
      if (t < 0) return;
      for (int v = 0; v < 2; ++v, a1 += 4, a3 += 6) put_vertex(a3, s16(rd16(a1)), 0, s16(rd16(a1 + 2)));
    }
  }

  // ---- polygon list ($3285a), full models ------------------------------------------
  void poly_list(uint32_t p, int distance) {
    wr32(0x5a9de, 0x000FFFFF);
    wr32(0x5a9e2, 0);
    for (int guard = 0; guard < 2000; ++guard) {
      uint16_t w = rd16(p);
      p += 2;
      if (w & 0x8000) {
        if (w == 0xFFFF) { note("op $ffff in list"); return; }
        if (!op(w, p) || (w & 0x4000)) return;
        continue;
      }
      if (w != 32767) {
        if (s16(w) < distance) {
          int16_t w2 = s16(rd16(p));
          p += 2;
          if (w2 < 0 || w2 < distance) return;
        } else {
          int16_t t = s16(rd16(p));
          p += 2;
          if (t >= 0) return;
        }
      }
      int16_t n = s16(rd16(p));                   // $328b6
      p += 2;
      if (n > 0) {
        uint16_t off = rd16(p);
        p += 2;
        xform_full(n, off);
      }
      w = rd16(p);
      p += 2;
      if (!op(w, p) || (w & 0x4000)) return;
    }
  }

  // BSP / group stream ($32666): collect the polygon lists of every branch.
  void collect_lists(uint32_t start, std::vector<uint32_t>& lists) {
    std::vector<uint32_t> todo{start};
    std::set<uint32_t> seen;
    auto add = [&](uint32_t l) {
      if (std::find(lists.begin(), lists.end(), l) == lists.end()) lists.push_back(l);
    };
    while (!todo.empty() && seen.size() < 4096) {
      uint32_t p = todo.back();
      todo.pop_back();
      if (!seen.insert(p).second) continue;
      for (int guard = 0; guard < 256; ++guard) {
        uint16_t d0 = rd16(p);
        p += 2;
        if (!(d0 & 0x8000)) {                     // plane node: two subtrees
          uint16_t d7 = rd16(p);
          p += 2;
          if ((d7 & 0xFC00) == 0xFC00) {
            todo.push_back(base_ + uint32_t(int32_t(s16(rd16(p)))));
            break;
          }
          if (!(d7 & 0x0C00)) p += (d7 & 0x2000) ? 12 : 4;
          todo.push_back(base_ + uint32_t(int32_t(s16(rd16(p + 2)))));
          todo.push_back(base_ + uint32_t(int32_t(s16(rd16(p)))));
          break;
        }
        if (d0 == 0xFFFF) break;
        if (d0 & 0x2000) {                        // one list, then the object ends
          if (d0 & 0x1000) add(rd32(p));
          else add(base_ + (d0 & 4095));
          break;
        }
        uint32_t a1;
        if (d0 & 0x1000) {
          a1 = rd32(p);
          p += 4;
        } else {
          a1 = base_ + (d0 & 4095);
        }
        for (int g2 = 0; g2 < 256; ++g2) {
          uint16_t r = rd16(a1);
          a1 += 2;
          if (r & 0x1000) {
            add(rd32(a1));
            a1 += 4;
          } else {
            add(base_ + (r & 4095));
          }
          if (r & 0x8000) break;
        }
        if (d0 & 0x4000) break;                   // last group
      }
    }
  }

  // ---- distant objects ($32db4 / $32ec8) -------------------------------------------
  void distant_stream(uint32_t p, int distance) {
    wr32(0x5a9de, 0x000FFFFF);
    wr32(0x5a9e2, 0);
    for (int guard = 0; guard < 2000; ++guard) {
      uint16_t w = rd16(p);
      p += 2;
      if (w & 0x8000) {
        if (w == 0xFFFF) return;
        if (!op(w, p) || (w & 0x4000)) return;
        continue;
      }
      for (int g2 = 0; g2 < 64 && w != 32767; ++g2) {   // $32f20
        if (s16(w) < distance) {                  // $32f0c
          w = rd16(p);
          p += 2;
          if (s16(w) < 0 || s16(w) < distance) return;
          p = rd32(p);
          continue;
        }
        int16_t t = s16(rd16(p));
        p += 2;
        if (t >= 0) p += 2;
        break;
      }
      uint16_t n = rd16(p);                       // $32f3a
      p += 2;
      if (n != 0) {
        uint16_t off = rd16(p);
        p += 2;
        xform_distant(s16(n), off);
      }
      w = rd16(p);
      p += 2;
      if (!op(w, p) || (w & 0x4000)) return;
    }
  }

  void choose_shift(int count, uint32_t at, int stride) {
    int m = 0;
    for (int k = 0; k < count; ++k)
      for (int i = 0; i < stride / 2; ++i)
        m = std::max(m, std::abs(int(s16(rd16(at + uint32_t(stride * k + 2 * i))))));
    shift_ = 0;
    while ((m >> shift_) > MAX_COORD && shift_ < 4) ++shift_;
  }

  void run(uint32_t e, int slot, int distance) {
    uint32_t fn = rd32(e);
    base_ = rd32(e + 4);
    uint32_t p = rd32(e + 8);
    wr16(0x5acac, uint32_t(slot));
    uint32_t rec = SCENARIO + 512u * uint32_t(slot & 15);
    wr16(0x5acae, rec - SCENARIO);
    for (uint32_t a = 0; a < 512; a += 2) wr16(rec + a, 0);
    wr16(rec, 0x40);                              // record active
    wr16(0x5adb0, 0);
    wr32(0x5ad70, uint32_t(-100));  // camera a little above the ground
    wr16(0x61284, 0);
    frame_setup(distance);

    if (is_distant_fn(fn)) {
      uint16_t w = 0;
      for (int guard = 0;; ++guard) {             // $32db4
        if (guard > 32) throw DecodeError("LOD loop");
        w = rd16(p);
        p += 2;
        if (s16(w) < 0) break;
        if (s16(w) >= distance) { p += 4; break; }
        uint32_t nxt = rd32(p);
        if (int32_t(nxt) < 0) return;             // nothing at this distance
        p = nxt;
      }
      if (w == 0xFFFF) return;
      mesh_ = rd32(p);
      p += 4;
      lod_->mesh = mesh_;
      lod_->stream = p;
      lod_->flags = 2;
      flat_ = true;
      wr32(0x5ad2a, mesh_);
      int count = s16(rd16(mesh_));
      lod_->base_verts = count;
      choose_shift(count, mesh_ + 6, 4);
      for (int pass = 0; pass < 2; ++pass) {
        capture_ = pass == 1;
        xform_distant(count, 0);
        distant_stream(p, distance);
      }
      capture_ = false;
      return;
    }

    uint16_t desc = 0;
    for (int guard = 0;; ++guard) {               // $31da4
      if (guard > 32) throw DecodeError("LOD loop");
      uint16_t w = rd16(p);
      p += 2;
      if (w & 0x8000) { desc = w; break; }
      if (int(w & 0x3fff) > distance) {
        if (!(w & 0x4000)) p += 2;
        desc = rd16(p);
        p += 2;
        break;
      }
      if (w & 0x4000) { p += 4; continue; }
      int16_t x = s16(rd16(p));
      if (x < 0) return;                          // nothing at this distance
      p = base_ + uint32_t(int32_t(x));
    }
    if (desc == 0xFFFF) return;
    mesh_ = base_ + (desc & 4095);
    wr32(0x5ad2a, mesh_);
    wr32(0x5ad2e, p);
    lod_->mesh = mesh_;
    lod_->stream = p;
    flags_ = rd8(mesh_ + 7);
    lod_->flags = flags_;
    const bool articulated = flags_ & 1;          // $31f36: scenario object
    flat_ = !articulated && (flags_ & 2);
    int count = rd8(mesh_ + 8);
    // articulated: +8 = all vertices (object-rotated into record +164),
    // +9 = the ones also projected into the pool
    int pooled = articulated ? std::min<int>(count, rd8(mesh_ + 9)) : count;
    lod_->base_verts = count;
    choose_shift(count, mesh_ + 10, flat_ ? 4 : 6);

    // View matrix and eye for the ops that project vertices themselves
    // ($33936: the ground shadow of scenario objects, from record +164):
    // identity, camera Z_OFFSET in front, ground just below the model.
    for (uint32_t i = 0; i < 9; ++i) wr16(0x5aed0 + 2 * i, i % 4 == 0 ? 256 : 0);
    int ground = 0;
    for (int k = 0; k < count && !flat_; ++k)
      ground = std::min(ground, int(s16(rd16(mesh_ + 10 + 6u * uint32_t(k) + 2))));
    wr32(FRAME - 28, uint32_t(((ground >> shift_) - 1) * 256));
    wr32(0x5ae28, 0);
    wr32(0x5ae30, uint32_t(Z_OFFSET * 256));

    std::vector<uint32_t> lists;
    collect_lists(p, lists);
    for (int pass = 0; pass < 2; ++pass) {        // pass 0 only fills the pool
      capture_ = pass == 1;
      uint32_t a1 = mesh_ + 10 + uint32_t(count * (flat_ ? 4 : 6));
      xform_full(pooled, 0);
      if (articulated) {                          // object rotation = identity
        for (int k = 0; k < std::min(count, 58); ++k)
          for (uint32_t i = 0; i < 6; i += 2)
            wr16(rec + 164 + 6u * uint32_t(k) + i,
                 asr16(rd16(mesh_ + 10 + 6u * uint32_t(k) + i), uint32_t(shift_)));
      } else if (!flat_ && (flags_ & 8)) {
        extra_runs_3d(a1, POOL + 6u * uint32_t(count));
      } else if (flat_ && (flags_ & 4)) {
        extra_runs_flat(a1, POOL + 6u * uint32_t(count));
      }
      for (uint32_t l : lists) poly_list(l, distance);
    }
    capture_ = false;
    // The shadow is flat at the ground height the game passes in; put the
    // ground just below the model.
    if (!shadows_.empty()) {
      int lowest = 0;
      for (size_t i = 0; i < lod_->prims.size(); ++i)
        if (std::find(shadows_.begin(), shadows_.end(), i) == shadows_.end())
          for (const auto& v : lod_->prims[i].v) lowest = std::min(lowest, int(v.y));
      for (size_t i : shadows_)
        for (auto& v : lod_->prims[i].v) v.y = lowest - 1;
    }
  }
};

// ---- rendering ---------------------------------------------------------------------

// COLOR00..15 in flight (scenario free1; 5 = sky, 6 = sea).
const uint16_t kFlightPalette[16] = {0x000, 0x800, 0x888, 0x111, 0x151, 0x447, 0x036, 0x444,
                                     0x555, 0xd92, 0x666, 0x030, 0x333, 0x777, 0x777, 0x333};

uint32_t rgb12(uint16_t c) {
  uint32_t r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
  return 0xFF000000u | (r * 17) << 16 | (g * 17) << 8 | (b * 17);
}
uint32_t mix(uint32_t a, uint32_t b, int t) {   // t/256 of b
  auto ch = [&](int s) {
    int x = int((a >> s) & 255), y = int((b >> s) & 255);
    return uint32_t((x * (256 - t) + y * t) >> 8) << s;
  };
  return 0xFF000000u | ch(16) | ch(8) | ch(0);
}

struct Raster {
  std::vector<uint32_t>& fb;
  std::vector<float> zb;
  int w, h, thick;
  Raster(std::vector<uint32_t>& f, int ww, int hh)
      : fb(f), zb(size_t(ww) * size_t(hh), 1e30f), w(ww), h(hh), thick(std::max(1, std::min(ww, hh) / 300)) {}

  void plot(int x, int y, float z, uint32_t c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    size_t i = size_t(y) * size_t(w) + size_t(x);
    if (z > zb[i]) return;
    fb[i] = c;
    zb[i] = z;
  }
  void tri(const float* a, const float* b, const float* c, uint32_t col) {
    float minx = std::min({a[0], b[0], c[0]}), maxx = std::max({a[0], b[0], c[0]});
    float miny = std::min({a[1], b[1], c[1]}), maxy = std::max({a[1], b[1], c[1]});
    int x0 = std::max(0, int(std::floor(minx))), x1 = std::min(w - 1, int(std::ceil(maxx)));
    int y0 = std::max(0, int(std::floor(miny))), y1 = std::min(h - 1, int(std::ceil(maxy)));
    float area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    if (std::fabs(area) < 1e-6f) return;
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        float px = float(x) + 0.5f, py = float(y) + 0.5f;
        float w0 = ((b[0] - px) * (c[1] - py) - (b[1] - py) * (c[0] - px)) / area;
        float w1 = ((c[0] - px) * (a[1] - py) - (c[1] - py) * (a[0] - px)) / area;
        float w2 = 1.f - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        plot(x, y, w0 * a[2] + w1 * b[2] + w2 * c[2], col);
      }
  }
  void line(const float* a, const float* b, uint32_t col, float bias) {
    float dx = b[0] - a[0], dy = b[1] - a[1];
    int n = int(std::max(std::fabs(dx), std::fabs(dy))) + 1;
    for (int i = 0; i <= n; ++i) {
      float t = float(i) / float(n);
      int x = int(a[0] + dx * t), y = int(a[1] + dy * t);
      for (int oy = 0; oy < thick; ++oy)
        for (int ox = 0; ox < thick; ++ox) plot(x + ox, y + oy, a[2] + (b[2] - a[2]) * t - bias, col);
    }
  }
};

void render_model(std::vector<uint32_t>& fb, int w, int h, const Lod& lod, float yaw, float pitch,
                  float zoom, bool wire, const uint16_t pal[16]) {
  std::fill(fb.begin(), fb.end(), rgb12(pal[5]));  // sky
  if (lod.prims.empty()) return;
  float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
  for (const auto& p : lod.prims)
    for (const auto& v : p.v) {
      float c[3] = {float(v.x), float(v.y), float(v.z)};
      for (int i = 0; i < 3; ++i) {
        lo[i] = std::min(lo[i], c[i]);
        hi[i] = std::max(hi[i], c[i]);
      }
    }
  float mid[3], r = 1.f;
  for (int i = 0; i < 3; ++i) mid[i] = (lo[i] + hi[i]) * 0.5f;
  for (const auto& p : lod.prims)
    for (const auto& v : p.v)
      r = std::max(r, std::hypot(float(v.x) - mid[0], float(v.y) - mid[1], float(v.z) - mid[2]));
  const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
  const float dist = 3.f * r, focal = float(std::min(w, h)) * 1.2f * zoom;
  // Game view space: x right, y up, z forward.  Orbit camera around mid.
  auto project = [&](const Vec3& v, float* o) {
    float x = float(v.x) - mid[0], y = float(v.y) - mid[1], z = float(v.z) - mid[2];
    float x1 = x * cy - z * sy, z1 = x * sy + z * cy;
    float y2 = y * cp - z1 * sp, z2 = y * sp + z1 * cp;
    float d = z2 + dist;
    if (d < r * 0.05f) d = r * 0.05f;
    o[0] = float(w) * 0.5f + x1 * focal / d;
    o[1] = float(h) * 0.5f - y2 * focal / d;
    o[2] = d;
  };
  Raster ras(fb, w, h);
  std::vector<float> pts;
  // Partial-plane / XOR primitives (shadows, ground patches) are resolved
  // against the land colour and drawn after, slightly in front, since they
  // usually lie on other polygons.
  constexpr uint8_t kGround = 4;
  for (int pass = 0; pass < 2; ++pass)
    for (const auto& p : lod.prims) {
      bool overlay = p.xr || p.mask != 15;
      if (p.kind != Prim::Poly || overlay != (pass == 1)) continue;
      pts.resize(p.v.size() * 3);
      for (size_t i = 0; i < p.v.size(); ++i) {
        project(p.v[i], &pts[i * 3]);
        if (overlay) pts[i * 3 + 2] -= r * 0.005f;
      }
      uint32_t col = rgb12(pal[p.index(kGround)]);
      for (size_t i = 1; i + 1 < p.v.size(); ++i)
        ras.tri(&pts[0], &pts[i * 3], &pts[(i + 1) * 3], col);
      if (wire)
        for (size_t i = 0; i < p.v.size(); ++i)
          ras.line(&pts[i * 3], &pts[((i + 1) % p.v.size()) * 3], mix(col, 0xFF000000u, 120),
                   r * 0.01f);
    }
  for (const auto& p : lod.prims) {
    if (p.kind == Prim::Poly) continue;
    uint32_t col = rgb12(pal[p.index(kGround)]);
    float a[3], b[3];
    project(p.v[0], a);
    if (p.kind == Prim::Line && p.v.size() >= 2) {
      project(p.v[1], b);
      ras.line(a, b, col, r * 0.01f);
    } else {
      int k = ras.thick + 1;
      for (int dy = -k; dy <= k; ++dy)
        for (int dx = -k; dx <= k; ++dx) ras.plot(int(a[0]) + dx, int(a[1]) + dy, a[2] - r * 0.01f, col);
    }
  }
}

std::string type_list(const Model& m) {
  std::string s;
  for (size_t i = 0; i < m.types.size(); ++i) {
    int t = m.types[i];  // negative: scenario-object template -t
    s += (i ? "," : "") + (t < 0 ? "S" + std::to_string(-t) : std::to_string(t));
  }
  return s;
}

bool same_lods(const std::vector<Lod>& a, const std::vector<Lod>& b) {
  auto same_v = [](const Vec3& p, const Vec3& q) { return p.x == q.x && p.y == q.y && p.z == q.z; };
  auto same_p = [&](const Prim& p, const Prim& q) {
    return p.kind == q.kind && p.colour == q.colour && p.mask == q.mask && p.xr == q.xr &&
           std::equal(p.v.begin(), p.v.end(), q.v.begin(), q.v.end(), same_v);
  };
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].mesh != b[i].mesh || a[i].distance != b[i].distance ||
        !std::equal(a[i].prims.begin(), a[i].prims.end(), b[i].prims.begin(), b[i].prims.end(), same_p))
      return false;
  return true;
}

void count_prims(const Lod& l, int& polys, int& lines, int& points) {
  polys = lines = points = 0;
  for (const auto& p : l.prims)
    (p.kind == Prim::Poly ? polys : p.kind == Prim::Line ? lines : points)++;
}

}  // namespace

std::vector<Model> load_models(const std::vector<uint8_t>& exe) {
  std::vector<Model> models;
  std::vector<uint8_t> img;
  if (!relocate(exe, img)) return models;
  auto r32 = [&](uint32_t a) {
    return uint32_t(img[a]) << 24 | uint32_t(img[a + 1]) << 16 | uint32_t(img[a + 2]) << 8 | img[a + 3];
  };
  Decoder dec(img);
  auto add_model = [&](uint32_t e, int slot, int label) {
    Model m;
    m.types.push_back(label);
    m.draw_fn = r32(e);
    m.base = r32(e + 4);
    m.lod_list = r32(e + 8);
    m.distant = is_distant_fn(m.draw_fn);
    std::set<std::pair<uint32_t, uint32_t>> seen;
    for (int d : dec.lod_distances(e)) {
      Lod l = dec.decode(e, slot, d);
      if (l.mesh == 0 && l.prims.empty()) continue;
      if (!seen.insert({l.mesh, l.stream}).second) continue;
      m.lods.push_back(std::move(l));
    }
    if (m.lods.empty()) return;
    // Several types share one model (same data, same result): list them once.
    auto same = std::find_if(models.begin(), models.end(), [&](const Model& o) {
      return o.distant == m.distant && same_lods(o.lods, m.lods);
    });
    if (same != models.end()) same->types.push_back(label);
    else models.push_back(std::move(m));
  };
  // Scenario objects (aircraft, ships...): templates installed per mission
  // into the type slot of their scenario record.  Labelled -1..-16.
  for (int k = 0; k < NUM_TEMPLATES; ++k) {
    uint32_t e = TEMPLATES + 20u * uint32_t(k);
    if (is_draw_fn(r32(e))) add_model(e, TEMPLATE_SLOT, -(k + 1));
  }
  bool started = false;
  for (int t = 0; t < MAX_TYPES; ++t) {
    uint32_t e = TYPE_TABLE + 20u * uint32_t(t);
    uint32_t fn = r32(e);
    if (fn == 0 && !started) continue;
    if (!is_draw_fn(fn)) break;                   // end of the table
    started = true;
    add_model(e, t, t);
  }
  return models;
}

void render_frame(std::vector<uint32_t>& fb, int w, int h, const std::vector<Model>& models,
                  ViewState& st, const uint16_t live_pal[16], DrawTextFn draw_text) {
  std::fill(fb.begin(), fb.end(), 0xFF181820u);
  if (models.empty()) {
    if (draw_text) draw_text(fb, w, h, 16, 16, "No models decoded", 2, 0xFFFF6060u);
    return;
  }
  int n = int(models.size());
  st.model = ((st.model % n) + n) % n;
  const Model& m = models[size_t(st.model)];
  int nl = int(m.lods.size());
  st.lod = std::clamp(st.lod, 0, nl - 1);
  const Lod& l = m.lods[size_t(st.lod)];
  const uint16_t* pal = st.live_palette && live_pal ? live_pal : kFlightPalette;
  render_model(fb, w, h, l, st.yaw, st.pitch, st.zoom, st.wire, pal);

  if (!draw_text) return;
  const int sc = std::max(1, w / 320);
  int polys, lines, points;
  count_prims(l, polys, lines, points);
  char line[160];
  std::snprintf(line, sizeof(line), "MODEL %d/%d  type %s  %s", st.model + 1, n, type_list(m).c_str(),
                m.types[0] < 0 ? "(scenario object)" : m.distant ? "(ground)" : "");
  draw_text(fb, w, h, 8 * sc, 8 * sc, line, sc, 0xFFFFD040u);
  std::snprintf(line, sizeof(line), "LOD %d/%d  dist %d  mesh $%x  %d verts", st.lod + 1, nl, l.distance,
                l.mesh, l.base_verts);
  draw_text(fb, w, h, 8 * sc, 20 * sc, line, sc, 0xFFB0B0B0u);
  std::snprintf(line, sizeof(line), "%d polygons  %d lines  %d points", polys, lines, points);
  draw_text(fb, w, h, 8 * sc, 32 * sc, line, sc, 0xFFB0B0B0u);
  if (!l.notes.empty()) draw_text(fb, w, h, 8 * sc, 44 * sc, "note: " + l.notes[0], sc, 0xFFFF8060u);
  draw_text(fb, w, h, 8 * sc, h - 28 * sc, "Left/Right model  Up/Down LOD  WASD/stick orbit  +/- zoom",
            sc, 0xFF808090u);
  draw_text(fb, w, h, 8 * sc, h - 16 * sc,
            std::string("Tab outline  P palette (") + (st.live_palette ? "live" : "flight") +
                ")  Esc/B close",
            sc, 0xFF808090u);
}

bool dump_models(const std::vector<uint8_t>& exe, const std::string& dir) {
  auto models = load_models(exe);
  if (models.empty()) return false;
  const int W = 480, H = 360;
  std::vector<uint32_t> fb(size_t(W) * H);
  for (size_t i = 0; i < models.size(); ++i) {
    const Model& m = models[i];
    for (size_t k = 0; k < m.lods.size(); ++k) {
      const Lod& l = m.lods[k];
      int polys, lines, points;
      count_prims(l, polys, lines, points);
      std::printf("model %2zu type %-8s fn $%x lod %zu dist %5d mesh $%05x flags %02x verts %3d  "
                  "%3d polys %3d lines %2d points%s%s\n",
                  i + 1, type_list(m).c_str(), m.draw_fn, k, l.distance, l.mesh, l.flags,
                  l.base_verts, polys, lines, points, l.notes.empty() ? "" : "  note: ",
                  l.notes.empty() ? "" : l.notes[0].c_str());
      if (std::getenv("FA18_MESH_PRIMS"))
        for (const auto& p : l.prims) {
          std::printf("   %c c%2d m%2d x%2d", "PLD"[p.kind], p.colour, p.mask, p.xr);
          for (const auto& v : p.v) std::printf(" (%d,%d,%d)", v.x, v.y, v.z);
          std::printf("\n");
        }
      render_model(fb, W, H, l, 0.7f, 0.45f, 1.f, true, kFlightPalette);
      char name[64];
      std::snprintf(name, sizeof(name), "/model%03zu_lod%zu.ppm", i + 1, k);
      FILE* f = std::fopen((dir + name).c_str(), "wb");
      if (!f) return false;
      std::fprintf(f, "P6\n%d %d\n255\n", W, H);
      for (uint32_t c : fb) {
        uint8_t rgb[3] = {uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c)};
        std::fwrite(rgb, 1, 3, f);
      }
      std::fclose(f);
    }
  }
  return true;
}

}  // namespace mesh_viewer
