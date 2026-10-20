// World database: decode / text format / apply.  See world.hpp and
// docs/world_format.md.
#include "native/world.hpp"

#include "amiga/hunk.hpp"
#include "amiga/machine.hpp"
#include "native/runtime.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace world {

namespace {

constexpr uint32_t kRowArea = 0x5788c;     // after the three row offset arrays
constexpr uint32_t kRowAreaEnd = 0x57cc4;  // end of hunk 66
constexpr uint32_t kTypeTable = 0x36094;
constexpr uint32_t kTemplates = 0x35f54;
constexpr uint32_t kCompactPos = 0x3080e;  // 16 x (dx, dz) signed bytes
constexpr int kMaxPerSubcell = 16;         // $3039a: 16 entries per slot list

struct Mem {
  const Reader& rd;
  uint8_t b(uint32_t a) const { return rd(a); }
  int8_t sb(uint32_t a) const { return int8_t(rd(a)); }
  uint16_t w(uint32_t a) const { return uint16_t(rd(a) << 8 | rd(a + 1)); }
  int16_t sw(uint32_t a) const { return int16_t(w(a)); }
  uint32_t l(uint32_t a) const { return uint32_t(w(a)) << 16 | w(a + 2); }
};

std::string hex(uint32_t v, int digits = 0) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%0*x", digits, v);
  return buf;
}

// ---- maps ---------------------------------------------------------------------

bool decode_cell(const Mem& m, uint32_t p, std::vector<Object>& out, std::string& err) {
  for (int guard = 0; guard < 256; ++guard) {
    uint8_t b0 = m.b(p++);
    if (b0 == 0xFF) return true;
    uint8_t b1 = m.b(p++);
    Object o;
    o.subcell = b0 & 15;
    o.pos = b0 >> 4;
    o.type = b1 & 0x7F;
    o.compact = (b1 & 0x80) != 0;
    if (!o.compact) {
      o.x = m.sw(p);
      o.z = m.sw(p + 2);
      p += 4;
    }
    out.push_back(o);
  }
  err = "cell list at $" + hex(p) + " is not terminated";
  return false;
}

bool decode_map(const Mem& m, MapId id, Map& map, std::string& err) {
  const uint32_t base = kMapBase[id];
  for (int r = 0; r < kMapRows; ++r) {
    int16_t off = m.sw(base + 2 * uint32_t(r));
    if (off <= 0) {
      err = std::string(kMapName[id]) + ": row " + std::to_string(r) + " has offset <= 0";
      return false;
    }
    uint32_t a = base + uint32_t(off);
    int16_t n = m.sw(a);
    if (n < 0) continue;
    int keys = n >> 1;
    uint32_t ptrs = a + 2 + uint32_t(n);
    for (int k = 0; k < keys; ++k) {
      Cell c;
      c.z = r;
      c.x = m.sw(a + 2 + 2 * uint32_t(k));
      if (!decode_cell(m, m.l(ptrs + 4 * uint32_t(k)), c.objects, err)) return false;
      map.cells.push_back(std::move(c));
    }
  }
  return true;
}

// Neighbour subcell origin of an object, units.
int32_t sub_dx(uint8_t sub) { return 3 - (sub & 3); }
int32_t sub_dz(uint8_t sub) { return 3 - (sub >> 2); }

// ---- scripts ------------------------------------------------------------------

bool decode_scripts(const Mem& m, int t, ScriptTable& st, std::string& err) {
  st.base = kScriptBase[t];
  for (int i = 0; i < kScriptCount[t]; ++i) {
    int16_t off = m.sw(st.base + 2 * uint32_t(i));
    if (off <= 0) {
      err = "script table $" + hex(st.base) + " entry " + std::to_string(i) + ": bad offset";
      return false;
    }
    uint32_t a = st.base + uint32_t(off);
    Script s;
    for (int guard = 0; guard < 64; ++guard) {
      uint8_t nb = m.b(a++);
      if (nb & 0x80) break;
      ScriptStep step;
      step.neighbour = nb;
      uint8_t n = m.b(a++);
      for (int k = 0; k < n; ++k) step.subcells.push_back(m.b(a++));
      s.steps.push_back(std::move(step));
    }
    st.scripts.push_back(std::move(s));
  }
  return true;
}

// ---- mission data ---------------------------------------------------------------

Spawn read_spawn(const Mem& m, uint32_t a) {
  return Spawn{m.w(a), m.w(a + 2), m.w(a + 4), m.w(a + 6), m.w(a + 8)};
}
std::vector<Spawn> read_spawn_list(const Mem& m, uint32_t a) {
  std::vector<Spawn> v;
  for (int guard = 0; guard < 64 && m.sw(a) >= 0; ++guard, a += 10) v.push_back(read_spawn(m, a));
  return v;
}
Waypoint read_wp(const Mem& m, uint32_t a) {
  return Waypoint{m.sw(a), m.sw(a + 2), m.sw(a + 4), m.sw(a + 6), m.sw(a + 8)};
}

void decode_missions(const Mem& m, World& w) {
  std::map<uint32_t, std::string> lists;
  for (int mi = 0; mi < kMissions; ++mi)
    for (int v = 0; v < kVariants; ++v) {
      uint32_t e = kMissionTable + uint32_t(mi * 12 + v * 4);
      MissionEntry me;
      me.mission = mi + 1;
      me.variant = v;
      me.list = kMissionTable + uint32_t(int32_t(m.sw(e)));
      me.max_count = m.sw(e + 2);
      for (int l = 0; l < kLevels; ++l) {
        me.level_off[l] = m.w(me.list + 2 * uint32_t(l));
        uint32_t a = me.list + uint32_t(int32_t(int16_t(me.level_off[l])));
        std::string& u = lists[a];
        u += (u.empty() ? "" : " ") + std::string("m") + std::to_string(mi + 1) + "v" +
             std::to_string(v) + "l" + std::to_string(l);
      }
      w.missions.push_back(me);
    }
  for (auto& [a, users] : lists) w.spawn_lists.push_back(SpawnList{a, read_spawn_list(m, a), users});
  for (int i = 0; i < 8 && m.sw(kZoneTable + 4 * uint32_t(i)) >= 0; ++i) {
    Zone z;
    z.addr = m.l(kZoneTable + 4 * uint32_t(i));
    z.x1 = m.sw(z.addr);
    z.x2 = m.sw(z.addr + 2);
    z.z1 = m.sw(z.addr + 4);
    z.z2 = m.sw(z.addr + 6);
    int n = m.sw(z.addr + 8);
    for (int k = 0; k < n; ++k) z.spawns.push_back(read_spawn(m, z.addr + 10 + 10 * uint32_t(k)));
    w.zones.push_back(std::move(z));
  }
  // Waypoint records: follow every position like a route, but stop at the
  // other structures of hunk 27 (routes are only terminated by a negative
  // word, so the chains would run on into the zone and spawn data).
  std::vector<std::pair<uint32_t, uint32_t>> claimed = {
      {kRouteTable, kRouteTable + 2 * kPositions},
      {kZoneTable, kZoneTable + 4 * uint32_t(w.zones.size()) + 2},
      {kMissionTable, kMissionTable + 12 * kMissions}};
  for (const MissionEntry& me : w.missions) claimed.push_back({me.list, me.list + 2 * kLevels});
  for (const SpawnList& sl : w.spawn_lists)
    claimed.push_back({sl.addr, sl.addr + 10 * uint32_t(sl.spawns.size()) + 2});
  for (const Zone& z : w.zones) claimed.push_back({z.addr, z.addr + 10 + 10 * uint32_t(z.spawns.size())});
  auto is_claimed = [&](uint32_t a) {
    for (auto& [lo, hi] : claimed)
      if (a < hi && a + 10 > lo) return true;
    return false;
  };
  std::map<uint16_t, Waypoint> recs;
  for (int i = 0; i < kPositions; ++i) {
    uint16_t off = m.w(kRouteTable + 2 * uint32_t(i));
    w.positions.push_back(off);
    if (off == 0) continue;
    for (uint32_t a = kRouteTable + off; a + 10 <= kHunk27End && m.sw(a) >= 0 && !is_claimed(a); a += 10)
      recs.emplace(uint16_t(a - kRouteTable), read_wp(m, a));
  }
  for (auto& [off, wp] : recs) w.waypoints.push_back(WaypointRec{off, wp});
  for (int i = 0; i < 16; ++i) {
    uint32_t a = kTemplates + 20 * uint32_t(i);
    w.templates.push_back(Template{m.l(a), m.l(a + 4), m.l(a + 8), m.l(a + 12), m.l(a + 16)});
  }
}

// ---- text helpers -----------------------------------------------------------------

std::string spawn_text(const Spawn& s) {
  char buf[160];
  std::snprintf(buf, sizeof(buf), "%04x %04x %04x %04x %04x", s.tmpl, s.kind, s.slot, s.pos,
                s.flags);
  std::string t = buf;
  std::snprintf(buf, sizeof(buf), "   # template %d, class $%02x, slot %d, route %d, pos %d",
                s.tmpl / 20, s.kind & 0xF0, s.slot & 127, (s.slot >> 8) & 127, s.pos / 2);
  return t + buf;
}

bool parse_spawn(std::istringstream& in, Spawn& s) {
  unsigned v[5];
  for (unsigned& x : v)
    if (!(in >> std::hex >> x) || x > 0xFFFF) return false;
  s = Spawn{uint16_t(v[0]), uint16_t(v[1]), uint16_t(v[2]), uint16_t(v[3]), uint16_t(v[4])};
  return true;
}

std::string strip_comment(const std::string& s) {
  size_t p = s.find('#');
  return p == std::string::npos ? s : s.substr(0, p);
}

int hexdigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

// ---- coordinates ------------------------------------------------------------------

void object_units(MapId m, const Cell& c, const Object& o, int32_t& X, int32_t& Z) {
  int32_t ox = o.x, oz = o.z;
  if (o.compact) {
    // Values of the table at $3080e (fixed in the executable).
    static const int8_t tab[16][2] = {{-32, -32}, {-96, -32}, {96, -32},  {32, -32},
                                      {-32, -96}, {-96, -96}, {96, -96},  {32, -96},
                                      {-32, 96},  {-96, 96},  {96, 96},   {32, 96},
                                      {-32, 32},  {-96, 32},  {96, 32},   {32, 32}};
    ox = tab[o.pos][0];
    oz = tab[o.pos][1];
  }
  if (m == Near) {
    X = c.x * 16384 + sub_dx(o.subcell) * 4096 + ox;
    Z = c.z * 16384 + sub_dz(o.subcell) * 4096 + oz;
  } else {
    X = c.x * 65536 + sub_dx(o.subcell) * 16384 + 4 * ox;
    Z = c.z * 65536 + sub_dz(o.subcell) * 16384 + 4 * oz;
  }
}

bool object_from_units(MapId m, int cx, int cz, int32_t X, int32_t Z, Object& o,
                       std::string& err) {
  const int32_t cell = m == Near ? 16384 : 65536, quarter = cell / 4;
  int32_t lx = X - cx * cell, lz = Z - cz * cell;
  if (lx < 0 || lz < 0 || lx >= cell || lz >= cell) {
    err = "position outside its cell";
    return false;
  }
  int32_t sx = lx / quarter, sz = lz / quarter;
  o.subcell = uint8_t((3 - sx) + 4 * (3 - sz));
  lx -= sx * quarter;
  lz -= sz * quarter;
  if (m != Near) {
    if ((lx | lz) & 3) {
      err = "far map positions must be multiples of 4 units";
      return false;
    }
    lx /= 4;
    lz /= 4;
  }
  o.x = int16_t(lx);
  o.z = int16_t(lz);
  return true;
}

// ---- decode -------------------------------------------------------------------------

bool decode(const Reader& rd, World& w, std::string& err) {
  w = World{};
  Mem m{rd};
  for (int i = 0; i < kMaps; ++i) {
    if (!decode_map(m, MapId(i), w.maps[i], err)) return false;
    w.has_map[i] = true;
  }
  for (int t = 0; t < 3; ++t) {
    if (!decode_scripts(m, t, w.scripts[t], err)) return false;
    w.has_scripts[t] = true;
  }
  decode_missions(m, w);
  return true;
}

bool decode_exe(const std::vector<uint8_t>& exe, World& w, std::string& err) {
  amiga::HunkFile hf;
  if (!hf.parse(exe)) {
    err = "not a hunk executable";
    return false;
  }
  std::vector<uint32_t> addrs;
  uint32_t a = amiga::LOAD_BASE;
  for (auto& h : hf.hunks) {
    addrs.push_back(a + 8);
    a += 8 + h.alloc_size;
  }
  std::vector<uint8_t> img(a, 0);
  auto get32 = [&](uint32_t at) {
    return uint32_t(img[at]) << 24 | uint32_t(img[at + 1]) << 16 | uint32_t(img[at + 2]) << 8 |
           img[at + 3];
  };
  for (size_t i = 0; i < hf.hunks.size(); ++i) {
    const auto& h = hf.hunks[i];
    std::memcpy(&img[addrs[i]], h.data.data(), h.data.size());
    for (const auto& [target, offs] : h.relocs) {
      if (target >= addrs.size()) {
        err = "bad relocation";
        return false;
      }
      for (uint32_t o : offs) {
        uint32_t v = get32(addrs[i] + o) + addrs[target];
        for (int k = 0; k < 4; ++k) img[addrs[i] + o + uint32_t(k)] = uint8_t(v >> (24 - 8 * k));
      }
    }
  }
  return decode([&](uint32_t at) { return at < img.size() ? img[at] : uint8_t(0); }, w, err);
}

// ---- text -------------------------------------------------------------------------------

std::string to_text(const World& w) {
  std::ostringstream o;
  o << "fa18-world 1\n"
       "# F/A-18 Interceptor world database (format: docs/world_format.md).\n"
       "# Positions X Z are absolute, in units of 256 world units:\n"
       "#   near map: cell (x z) covers [x*16384, x*16384+16383]\n"
       "#   far maps: sector (x z) covers [x*65536, x*65536+65535], positions multiple of 4\n\n";
  for (int i = 0; i < kMaps; ++i) {
    o << "map " << kMapName[i] << "\n";
    for (const Cell& c : w.maps[i].cells) {
      o << "cell " << c.x << " " << c.z << "\n";
      for (const Object& ob : c.objects) {
        if (ob.compact) {
          o << "  objc " << int(ob.type) << " " << int(ob.subcell) << " " << int(ob.pos) << "\n";
          continue;
        }
        int32_t X, Z;
        object_units(MapId(i), c, ob, X, Z);
        o << "  obj " << int(ob.type) << " " << X << " " << Z;
        Object back;
        std::string e;
        if (!object_from_units(MapId(i), c.x, c.z, X, Z, back, e) || back.subcell != ob.subcell)
          o << " sub=" << int(ob.subcell);
        if (ob.pos) o << " hi=" << int(ob.pos);
        o << "\n";
      }
    }
    o << "\n";
  }
  static const char* script_comment[3] = {
      "per camera class (view heading 60..315 deg, $5aa7e = 1)",
      "per camera class x heading octant",
      "far map A at low altitude: per camera class x cell in sector"};
  for (int t = 0; t < 3; ++t) {
    const ScriptTable& st = w.scripts[t];
    o << "scripts " << hex(st.base) << "   # " << script_comment[t] << "\n";
    for (size_t i = 0; i < st.scripts.size(); ++i) {
      o << "script " << i;
      for (const ScriptStep& s : st.scripts[i].steps) {
        o << " " << int(s.neighbour) << ":";
        for (uint8_t sc : s.subcells) o << hex(sc);
        if (s.subcells.empty()) o << "-";
      }
      o << "\n";
    }
    o << "\n";
  }
  o << "# mission M VARIANT list=ADDR max=N levels=OFF0,OFF1,OFF2,OFF3 (offsets from the list)\n";
  for (const MissionEntry& me : w.missions) {
    o << "mission " << me.mission << " " << me.variant << " list=" << hex(me.list)
      << " max=" << me.max_count << " levels=";
    for (int l = 0; l < kLevels; ++l) o << (l ? "," : "") << int16_t(me.level_off[l]);
    o << "\n";
  }
  o << "\n# spawn TMPL KIND SLOT POS FLAGS (hex words, $3d320); lists are patched in place\n";
  for (const SpawnList& sl : w.spawn_lists) {
    o << "spawns " << hex(sl.addr) << "   # " << sl.users << "\n";
    for (const Spawn& s : sl.spawns) o << "  spawn " << spawn_text(s) << "\n";
  }
  o << "\n# pos INDEX OFFSET: the position table at $3ddc8 (offset from the table, 0 = unused)\n";
  for (size_t i = 0; i < w.positions.size(); ++i) o << "pos " << i << " " << w.positions[i] << "\n";
  o << "\n# wp OFFSET  cell-x cell-z offset-x offset-z height (waypoint records, patched in place)\n";
  for (const WaypointRec& r : w.waypoints)
    o << "wp " << r.offset << "  " << r.wp.cx << " " << r.wp.cz << " " << r.wp.ox << " " << r.wp.oz
      << " " << r.wp.h << "\n";
  o << "\n# zone ADDR X1 X2 Z1 Z2 (player cell bounds), then the spawns on entry\n";
  for (const Zone& z : w.zones) {
    o << "zone " << hex(z.addr) << " " << z.x1 << " " << z.x2 << " " << z.z1 << " " << z.z2
      << "\n";
    for (const Spawn& s : z.spawns) o << "  spawn " << spawn_text(s) << "\n";
  }
  o << "\n# newspawn MISSION TMPL KIND SLOT FLAGS CX CZ OX OZ H: an extra object created after\n"
       "# the mission's list (MISSION 0 = all); hex words as in spawn lines, position decimal\n";
  for (const ExtraSpawn& e : w.extras) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "newspawn %d %04x %04x %04x %04x %d %d %d %d %d\n", e.mission,
                  e.spawn.tmpl, e.spawn.kind, e.spawn.slot, e.spawn.flags, e.at.cx, e.at.cz, e.at.ox,
                  e.at.oz, e.at.h);
    o << buf;
  }
  o << "\n# scenario object templates ($35f54, read-only): draw model lod lod2 lod3\n";
  for (size_t i = 0; i < w.templates.size(); ++i) {
    const Template& t = w.templates[i];
    o << "template " << i << " " << hex(t.draw) << " " << hex(t.model) << " " << hex(t.lod) << " "
      << hex(t.lod2) << " " << hex(t.lod3) << "\n";
  }
  return o.str();
}

bool from_text(const std::string& text, World& w, std::string& err) {
  w = World{};
  std::istringstream in(text);
  std::string line;
  int lineno = 0;
  enum { None, InMap, InScripts, InSpawns, InZone } state = None;
  int map = -1, table = -1;
  Cell* cell = nullptr;
  std::vector<Spawn>* spawns = nullptr;
  bool header = false;
  auto fail = [&](const std::string& msg) {
    err = "line " + std::to_string(lineno) + ": " + msg;
    return false;
  };
  while (std::getline(in, line)) {
    ++lineno;
    std::istringstream ls(strip_comment(line));
    std::string kw;
    if (!(ls >> kw)) continue;
    if (!header) {
      int ver = 0;
      if (kw != "fa18-world" || !(ls >> ver) || ver != 1) return fail("expected 'fa18-world 1'");
      header = true;
      continue;
    }
    if (kw == "map") {
      std::string name;
      ls >> name;
      map = -1;
      for (int i = 0; i < kMaps; ++i)
        if (name == kMapName[i]) map = i;
      if (map < 0) return fail("unknown map '" + name + "'");
      if (w.has_map[map]) return fail("map '" + name + "' given twice");
      w.has_map[map] = true;
      state = InMap;
      cell = nullptr;
    } else if (kw == "cell") {
      if (state != InMap) return fail("'cell' outside a map");
      Cell c;
      if (!(ls >> c.x >> c.z) || c.x < 0 || c.z < 0 || c.x >= kMapRows || c.z >= kMapRows)
        return fail("bad cell coordinates");
      w.maps[map].cells.push_back(c);
      cell = &w.maps[map].cells.back();
    } else if (kw == "obj" || kw == "objc") {
      if (!cell) return fail("object outside a cell");
      Object o;
      int type = -1;
      if (!(ls >> type) || type < 0 || type > 127) return fail("bad type");
      o.type = uint8_t(type);
      if (kw == "objc") {
        int sub = -1, pos = -1;
        if (!(ls >> sub >> pos) || sub < 0 || sub > 15 || pos < 0 || pos > 15)
          return fail("objc needs TYPE SUBCELL POS (0..15)");
        o.compact = true;
        o.subcell = uint8_t(sub);
        o.pos = uint8_t(pos);
      } else {
        int32_t X, Z;
        if (!(ls >> X >> Z)) return fail("obj needs TYPE X Z");
        std::string e;
        std::string opt;
        int sub = -1;
        while (ls >> opt) {
          if (opt.rfind("sub=", 0) == 0) sub = std::atoi(opt.c_str() + 4);
          else if (opt.rfind("hi=", 0) == 0) o.pos = uint8_t(std::atoi(opt.c_str() + 3) & 15);
          else return fail("unknown option '" + opt + "'");
        }
        if (sub >= 0) {
          // Explicit subcell: position relative to that subcell's corner.
          if (sub > 15) return fail("bad sub=");
          o.subcell = uint8_t(sub);
          Object probe = o;
          probe.x = probe.z = 0;
          int32_t X0, Z0;
          object_units(MapId(map), *cell, probe, X0, Z0);
          int32_t dx = X - X0, dz = Z - Z0;
          if (map != Near) {
            if ((dx | dz) & 3) return fail("far map positions must be multiples of 4 units");
            dx /= 4;
            dz /= 4;
          }
          if (dx < -32768 || dx > 32767 || dz < -32768 || dz > 32767)
            return fail("position out of range");
          o.x = int16_t(dx);
          o.z = int16_t(dz);
        } else {
          uint8_t hi = o.pos;
          if (!object_from_units(MapId(map), cell->x, cell->z, X, Z, o, e)) return fail(e);
          o.pos = hi;
        }
      }
      cell->objects.push_back(o);
    } else if (kw == "scripts") {
      std::string b;
      ls >> b;
      uint32_t base = uint32_t(std::strtoul(b.c_str(), nullptr, 16));
      table = -1;
      for (int t = 0; t < 3; ++t)
        if (base == kScriptBase[t]) table = t;
      if (table < 0) return fail("unknown script table");
      if (w.has_scripts[table]) return fail("script table given twice");
      w.has_scripts[table] = true;
      w.scripts[table].base = base;
      state = InScripts;
    } else if (kw == "script") {
      if (state != InScripts) return fail("'script' outside a script table");
      size_t idx;
      if (!(ls >> idx) || idx != w.scripts[table].scripts.size()) return fail("script index out of order");
      Script s;
      std::string step;
      while (ls >> step) {
        size_t colon = step.find(':');
        if (colon == std::string::npos) return fail("script step must be N:SUBCELLS");
        ScriptStep st;
        int nb = std::atoi(step.substr(0, colon).c_str());
        if (nb < 0 || nb > 20) return fail("neighbour must be 0..20");
        st.neighbour = uint8_t(nb);
        for (size_t k = colon + 1; k < step.size(); ++k) {
          if (step[k] == '-') continue;
          int d = hexdigit(step[k]);
          if (d < 0) return fail("bad subcell digit");
          st.subcells.push_back(uint8_t(d));
        }
        s.steps.push_back(std::move(st));
      }
      w.scripts[table].scripts.push_back(std::move(s));
    } else if (kw == "mission") {
      MissionEntry me;
      std::string list, max, levels;
      if (!(ls >> me.mission >> me.variant >> list >> max >> levels) ||
          list.rfind("list=", 0) != 0 || max.rfind("max=", 0) != 0 ||
          levels.rfind("levels=", 0) != 0)
        return fail("mission M V list=ADDR max=N levels=a,b,c,d");
      me.list = uint32_t(std::strtoul(list.c_str() + 5, nullptr, 16));
      me.max_count = int16_t(std::atoi(max.c_str() + 4));
      std::istringstream lv(levels.substr(7));
      std::string item;
      for (int l = 0; l < kLevels; ++l) {
        if (!std::getline(lv, item, ',')) return fail("need 4 level offsets");
        me.level_off[l] = uint16_t(std::atoi(item.c_str()));
      }
      w.missions.push_back(me);
      state = None;
    } else if (kw == "spawns") {
      SpawnList sl;
      std::string a;
      ls >> a;
      sl.addr = uint32_t(std::strtoul(a.c_str(), nullptr, 16));
      w.spawn_lists.push_back(sl);
      spawns = &w.spawn_lists.back().spawns;
      state = InSpawns;
    } else if (kw == "spawn") {
      if (state != InSpawns && state != InZone) return fail("'spawn' outside a list or zone");
      Spawn s;
      if (!parse_spawn(ls, s)) return fail("spawn needs 5 hex words");
      spawns->push_back(s);
    } else if (kw == "pos") {
      size_t idx;
      unsigned off;
      if (!(ls >> idx >> off) || idx >= size_t(kPositions) || off > 0x7FFF) return fail("pos INDEX OFFSET");
      if (w.positions.size() <= idx) w.positions.resize(idx + 1, 0xFFFF);
      w.positions[idx] = uint16_t(off);
      state = None;
    } else if (kw == "wp") {
      WaypointRec r;
      unsigned off;
      if (!(ls >> off >> r.wp.cx >> r.wp.cz >> r.wp.ox >> r.wp.oz >> r.wp.h) || off > 0x7FFF)
        return fail("wp OFFSET CX CZ OX OZ H");
      r.offset = uint16_t(off);
      w.waypoints.push_back(r);
      state = None;
    } else if (kw == "zone") {
      Zone z;
      std::string a;
      if (!(ls >> a >> z.x1 >> z.x2 >> z.z1 >> z.z2)) return fail("zone ADDR X1 X2 Z1 Z2");
      z.addr = uint32_t(std::strtoul(a.c_str(), nullptr, 16));
      w.zones.push_back(z);
      spawns = &w.zones.back().spawns;
      state = InZone;
    } else if (kw == "newspawn") {
      ExtraSpawn e;
      unsigned v[4];
      if (!(ls >> std::dec >> e.mission)) return fail("newspawn MISSION TMPL KIND SLOT FLAGS CX CZ OX OZ H");
      for (unsigned& x : v)
        if (!(ls >> std::hex >> x) || x > 0xFFFF) return fail("newspawn: TMPL KIND SLOT FLAGS are hex words");
      if (!(ls >> std::dec >> e.at.cx >> e.at.cz >> e.at.ox >> e.at.oz >> e.at.h))
        return fail("newspawn: position CX CZ OX OZ H");
      if (e.mission < 0 || e.mission > 127 || v[0] % 20 || v[0] / 20 > 15 || (v[2] & 127) > 15 ||
          e.at.cx < 0)
        return fail("newspawn: bad mission, template (0..15 x 20), slot (0..15) or position");
      e.spawn = Spawn{uint16_t(v[0]), uint16_t(v[1]), uint16_t(v[2]), 0, uint16_t(v[3])};
      w.extras.push_back(e);
      state = None;
    } else if (kw == "template") {
      Template t;
      size_t idx;
      std::string v[5];
      if (!(ls >> idx >> v[0] >> v[1] >> v[2] >> v[3] >> v[4])) return fail("bad template line");
      uint32_t* f[5] = {&t.draw, &t.model, &t.lod, &t.lod2, &t.lod3};
      for (int k = 0; k < 5; ++k) *f[k] = uint32_t(std::strtoul(v[k].c_str(), nullptr, 16));
      w.templates.push_back(t);
      state = None;
    } else {
      return fail("unknown keyword '" + kw + "'");
    }
  }
  if (!header) {
    err = "empty world file";
    return false;
  }
  for (int i = 0; i < kMaps; ++i)
    std::stable_sort(w.maps[i].cells.begin(), w.maps[i].cells.end(), [](const Cell& a, const Cell& b) {
      return a.z != b.z ? a.z < b.z : a.x < b.x;
    });
  return true;
}

bool load_file(const std::string& path, World& w, std::string& err) {
  std::ifstream f(path);
  if (!f) {
    err = "cannot open " + path;
    return false;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  if (!from_text(ss.str(), w, err)) {
    err = path + ": " + err;
    return false;
  }
  return true;
}

// ---- validation -----------------------------------------------------------------------

bool validate(const World& w, const Reader& rd, std::string& err) {
  Mem m{rd};
  for (int i = 0; i < kMaps; ++i) {
    const Map& map = w.maps[i];
    for (size_t k = 0; k < map.cells.size(); ++k) {
      const Cell& c = map.cells[k];
      std::string where = std::string(kMapName[i]) + " cell " + std::to_string(c.x) + " " +
                          std::to_string(c.z) + ": ";
      if (k && map.cells[k - 1].x == c.x && map.cells[k - 1].z == c.z) {
        err = where + "given twice";
        return false;
      }
      int per_sub[16] = {};
      for (const Object& o : c.objects) {
        if (o.type < 16) {
          err = where + "type " + std::to_string(o.type) + " is a scenario slot (use 16..127)";
          return false;
        }
        if (m.l(kTypeTable + 20u * o.type) == 0) {
          err = where + "type " + std::to_string(o.type) + " has no model";
          return false;
        }
        if (o.compact && o.subcell == 15 && o.pos == 15) {
          err = where + "compact subcell 15 / pos 15 would read as the end marker";
          return false;
        }
        if (++per_sub[o.subcell] > kMaxPerSubcell) {
          err = where + "more than 16 objects in subcell " + std::to_string(o.subcell);
          return false;
        }
      }
    }
  }
  for (int t = 0; t < 3; ++t)
    if (!w.has_scripts[t]) {
      continue;
    } else if (w.scripts[t].scripts.size() != size_t(kScriptCount[t])) {
      err = "script table $" + hex(kScriptBase[t]) + " needs " + std::to_string(kScriptCount[t]) +
            " scripts";
      return false;
    } else {
      for (const Script& s : w.scripts[t].scripts) {
        if (s.steps.size() > 14) {
          err = "script with more than 14 neighbour cells ($30290 limit)";
          return false;
        }
        for (const ScriptStep& st : s.steps)
          for (uint8_t sc : st.subcells)
            if (sc > 15) {
              err = "script subcell > 15";
              return false;
            }
      }
    }
  return true;
}

// ---- apply ------------------------------------------------------------------------------

namespace {

std::vector<uint8_t> cell_bytes(const Cell& c) {
  // The scene builder ($3039a) restarts a subcell's slot list whenever the
  // subcell changes, so each subcell's objects must be contiguous; the
  // groups keep the order of their first object.
  std::vector<const Object*> sorted;
  int first[16];
  std::fill(first, first + 16, -1);
  for (size_t i = 0; i < c.objects.size(); ++i) {
    if (first[c.objects[i].subcell] < 0) first[c.objects[i].subcell] = int(i);
    sorted.push_back(&c.objects[i]);
  }
  std::stable_sort(sorted.begin(), sorted.end(), [&](const Object* a, const Object* b) {
    return first[a->subcell] < first[b->subcell];
  });
  std::vector<uint8_t> v;
  for (const Object* o : sorted) {
    v.push_back(uint8_t(o->pos << 4 | o->subcell));
    v.push_back(uint8_t(o->type | (o->compact ? 0x80 : 0)));
    if (!o->compact) {
      v.push_back(uint8_t(uint16_t(o->x) >> 8));
      v.push_back(uint8_t(o->x));
      v.push_back(uint8_t(uint16_t(o->z) >> 8));
      v.push_back(uint8_t(o->z));
    }
  }
  v.push_back(0xFF);
  return v;
}

struct Writer {
  amiga::Machine& m;
  void b(uint32_t a, uint8_t v) { m.w8(a, v); }
  void w(uint32_t a, uint16_t v) { m.w16(a, v); }
  void l(uint32_t a, uint32_t v) { m.w32(a, v); }
};

bool apply_maps(amiga::Machine& mach, const World& given, std::string& err) {
  Writer wr{mach};
  bool any = false;
  for (int i = 0; i < kMaps; ++i) any |= given.has_map[i];
  if (!any) return true;
  // All three maps share the row area: rebuild all of them, taking the
  // maps the file does not give from memory.
  World w;
  if (!decode([&](uint32_t a) { return mach.r8(a); }, w, err)) return false;
  for (int i = 0; i < kMaps; ++i)
    if (given.has_map[i]) w.maps[i] = given.maps[i];
  // Cell lists (deduplicated) into one block at the top of RAM.
  std::map<std::vector<uint8_t>, uint32_t> cell_at;
  std::vector<std::vector<uint8_t>> blobs;
  uint32_t total = 0;
  for (int i = 0; i < kMaps; ++i)
    for (const Cell& c : w.maps[i].cells) {
      auto bytes = cell_bytes(c);
      if (cell_at.emplace(bytes, total).second) {
        total += uint32_t(bytes.size());
        blobs.push_back(std::move(bytes));
      }
    }
  uint32_t block = mach.reserve_top(std::max<uint32_t>(total, 8));
  if (!block) {
    err = "no memory for the cell lists";
    return false;
  }
  for (auto& [bytes, off] : cell_at)
    for (size_t k = 0; k < bytes.size(); ++k) wr.b(block + off + uint32_t(k), bytes[k]);
  // Rows: count word, sorted keys, pointers; identical rows shared.
  uint32_t at = kRowArea;
  const uint32_t empty_row = at;
  wr.w(at, 0xFFFF);
  at += 2;
  std::map<std::vector<uint32_t>, uint32_t> row_at;
  for (int i = 0; i < kMaps; ++i) {
    const uint32_t base = kMapBase[i];
    std::vector<std::vector<const Cell*>> rows(kMapRows);
    for (const Cell& c : w.maps[i].cells) rows[size_t(c.z)].push_back(&c);
    for (int r = 0; r < kMapRows; ++r) {
      auto& cells = rows[size_t(r)];
      std::sort(cells.begin(), cells.end(), [](const Cell* a, const Cell* b) { return a->x < b->x; });
      uint32_t row = empty_row;
      if (!cells.empty()) {
        std::vector<uint32_t> key;  // keys then pointers
        for (const Cell* c : cells) key.push_back(uint32_t(c->x));
        for (const Cell* c : cells) key.push_back(block + cell_at[cell_bytes(*c)]);
        auto it = row_at.find(key);
        if (it != row_at.end()) {
          row = it->second;
        } else {
          const uint32_t n = uint32_t(cells.size());
          const uint32_t size = 2 + 6 * n;
          if (at + size > kRowAreaEnd) {
            err = "map rows need more than the " + std::to_string(kRowAreaEnd - kRowArea) +
                  " bytes of hunk 66 (fewer distinct rows/cells per row needed)";
            return false;
          }
          row = at;
          wr.w(at, uint16_t(2 * n));
          for (uint32_t k = 0; k < n; ++k) {
            wr.w(at + 2 + 2 * k, uint16_t(key[k]));
            wr.l(at + 2 + 2 * n + 4 * k, key[n + k]);
          }
          at += size;
          row_at.emplace(std::move(key), row);
        }
      }
      wr.w(base + 2 * uint32_t(r), uint16_t(row - base));
    }
  }
  return true;
}

bool apply_scripts(amiga::Machine& mach, const World& w, std::string& err) {
  Writer wr{mach};
  for (int t = 0; t < 3; ++t) {
    if (!w.has_scripts[t]) continue;
    const uint32_t base = kScriptBase[t];
    const uint32_t end = t < 2 ? kScriptBase[t + 1] : kScriptEnd;
    uint32_t at = base + 2 * uint32_t(kScriptCount[t]);
    std::map<std::vector<uint8_t>, uint32_t> seen;
    for (int i = 0; i < kScriptCount[t]; ++i) {
      std::vector<uint8_t> bytes;
      for (const ScriptStep& s : w.scripts[t].scripts[size_t(i)].steps) {
        bytes.push_back(s.neighbour);
        bytes.push_back(uint8_t(s.subcells.size()));
        bytes.insert(bytes.end(), s.subcells.begin(), s.subcells.end());
      }
      bytes.push_back(0xFF);
      auto it = seen.find(bytes);
      uint32_t pos;
      if (it != seen.end()) {
        pos = it->second;
      } else {
        if (at + bytes.size() > end) {
          err = "script table $" + hex(base) + " does not fit into its " +
                std::to_string(end - base) + " bytes";
          return false;
        }
        pos = at;
        for (size_t k = 0; k < bytes.size(); ++k) wr.b(at + uint32_t(k), bytes[k]);
        at += uint32_t(bytes.size());
        seen.emplace(std::move(bytes), pos);
      }
      wr.w(base + 2 * uint32_t(i), uint16_t(pos - base));
    }
  }
  return true;
}

bool apply_missions(amiga::Machine& mach, const World& w, std::string& err) {
  // Hunk 27 is packed and shared (spawn lists, routes and zones overlap),
  // with no free space: everything keeps its size and address, and only
  // values that differ from the original are written.  Afterwards the
  // changed items are read back to catch inconsistent edits of shared data.
  Writer wr{mach};
  Reader rd = [&](uint32_t a) { return mach.r8(a); };
  Mem mem{rd};
  World orig;
  if (!decode(rd, orig, err)) return false;
  auto put16 = [&](uint32_t a, uint16_t v) {
    if (mem.w(a) != v) wr.w(a, v);
  };
  auto put_spawn = [&](uint32_t a, const Spawn& s) {
    put16(a, s.tmpl);
    put16(a + 2, s.kind);
    put16(a + 4, s.slot);
    put16(a + 6, s.pos);
    put16(a + 8, s.flags);
  };
  auto same = [](const Spawn& a, const Spawn& b) { return std::memcmp(&a, &b, sizeof(Spawn)) == 0; };
  auto same_wp = [](const Waypoint& a, const Waypoint& b) {
    return std::memcmp(&a, &b, sizeof(Waypoint)) == 0;
  };
  struct Check { uint32_t addr; Spawn s; };
  std::vector<Check> spawn_checks;
  std::vector<WaypointRec> wp_checks;

  for (const MissionEntry& me : w.missions) {
    if (me.mission < 1 || me.mission > kMissions || me.variant < 0 || me.variant >= kVariants) {
      err = "bad mission entry";
      return false;
    }
    uint32_t e = kMissionTable + uint32_t((me.mission - 1) * 12 + me.variant * 4);
    int32_t off = int32_t(me.list) - int32_t(kMissionTable);
    if (off < 0 || me.list + 2 * kLevels > kHunk27End) {
      err = "mission list outside hunk 27";
      return false;
    }
    put16(e, uint16_t(off));
    put16(e + 2, uint16_t(me.max_count));
    for (int l = 0; l < kLevels; ++l) put16(me.list + 2 * uint32_t(l), me.level_off[l]);
  }
  std::map<uint32_t, const SpawnList*> orig_lists;
  for (const SpawnList& sl : orig.spawn_lists) orig_lists[sl.addr] = &sl;
  for (const SpawnList& sl : w.spawn_lists) {
    auto it = orig_lists.find(sl.addr);
    if (it == orig_lists.end() || it->second->spawns.size() != sl.spawns.size()) {
      err = "spawns $" + hex(sl.addr) + ": must keep the address and " +
            std::to_string(it == orig_lists.end() ? 0 : it->second->spawns.size()) +
            " entries (lists are patched in place)";
      return false;
    }
    for (size_t k = 0; k < sl.spawns.size(); ++k) {
      if (same(sl.spawns[k], it->second->spawns[k])) continue;
      if (int16_t(sl.spawns[k].tmpl) < 0) {
        err = "spawns $" + hex(sl.addr) + ": a negative first word ends the list";
        return false;
      }
      put_spawn(sl.addr + 10 * uint32_t(k), sl.spawns[k]);
      spawn_checks.push_back({sl.addr + 10 * uint32_t(k), sl.spawns[k]});
    }
  }
  if (w.zones.size() > orig.zones.size()) {
    err = "zones: at most " + std::to_string(orig.zones.size()) + " (patched in place)";
    return false;
  }
  for (size_t i = 0; i < w.zones.size(); ++i) {
    const Zone& z = w.zones[i];
    const Zone& o = orig.zones[i];
    if (z.addr != o.addr || z.spawns.size() != o.spawns.size()) {
      err = "zone $" + hex(z.addr) + ": must keep the address and " + std::to_string(o.spawns.size()) +
            " spawns";
      return false;
    }
    put16(z.addr, uint16_t(z.x1));
    put16(z.addr + 2, uint16_t(z.x2));
    put16(z.addr + 4, uint16_t(z.z1));
    put16(z.addr + 6, uint16_t(z.z2));
    for (size_t k = 0; k < z.spawns.size(); ++k) {
      if (same(z.spawns[k], o.spawns[k])) continue;
      put_spawn(z.addr + 10 + 10 * uint32_t(k), z.spawns[k]);
      spawn_checks.push_back({z.addr + 10 + 10 * uint32_t(k), z.spawns[k]});
    }
  }
  if (!w.positions.empty() && w.positions.size() != size_t(kPositions)) {
    err = "pos: need all " + std::to_string(kPositions) + " entries";
    return false;
  }
  for (size_t i = 0; i < w.positions.size(); ++i) {
    uint16_t off = w.positions[i];
    if (off == 0xFFFF || (off && kRouteTable + off + 10 > kHunk27End)) {
      err = "pos " + std::to_string(i) + ": missing or outside hunk 27";
      return false;
    }
    put16(kRouteTable + 2 * uint32_t(i), off);
  }
  std::map<uint16_t, Waypoint> orig_wp;
  for (const WaypointRec& r : orig.waypoints) orig_wp[r.offset] = r.wp;
  for (const WaypointRec& r : w.waypoints) {
    auto it = orig_wp.find(r.offset);
    if (it == orig_wp.end()) {
      err = "wp " + std::to_string(r.offset) + ": no waypoint record at this offset";
      return false;
    }
    if (same_wp(it->second, r.wp)) continue;
    if (r.wp.cx < 0) {
      err = "wp " + std::to_string(r.offset) + ": a negative cell x ends a route";
      return false;
    }
    uint32_t a = kRouteTable + r.offset;
    put16(a, uint16_t(r.wp.cx));
    put16(a + 2, uint16_t(r.wp.cz));
    put16(a + 4, uint16_t(r.wp.ox));
    put16(a + 6, uint16_t(r.wp.oz));
    put16(a + 8, uint16_t(r.wp.h));
    wp_checks.push_back(r);
  }
  for (const Check& c : spawn_checks)
    if (!same(read_spawn(mem, c.addr), c.s)) {
      err = "spawn at $" + hex(c.addr) + " overlaps other edited data inconsistently";
      return false;
    }
  for (const WaypointRec& r : wp_checks)
    if (!same_wp(read_wp(mem, kRouteTable + r.offset), r.wp)) {
      err = "wp " + std::to_string(r.offset) + " overlaps other edited data inconsistently";
      return false;
    }
  return true;
}

}  // namespace

namespace {
std::vector<ExtraSpawn> g_extras;
uint32_t g_entry = 0;  // 10-byte spawn entry in reserved memory
}  // namespace

bool apply(amiga::Machine& m, const World& w, std::string& err) {
  Reader rd = [&](uint32_t a) { return m.r8(a); };
  if (!validate(w, rd, err)) return false;
  if (!apply_maps(m, w, err) || !apply_scripts(m, w, err) || !apply_missions(m, w, err)) return false;
  g_extras = w.extras;
  if (!g_extras.empty()) {
    g_entry = m.reserve_top(16);
    if (!g_entry || m.r16(kRouteTable + 2) == 0) {
      err = "newspawn: no memory / position table";
      return false;
    }
  }
  return true;
}

// $3d320 takes the start position from the position table: for each extra
// object the record of position 1 is replaced by the wanted position while
// $3d2ea runs, then restored, so everything derived from the position
// (cell, subcell, height, aiming in $3cfec) is computed by the original.
void spawn_extras() {
  if (g_extras.empty()) return;
  using native::rd8;
  using native::rd16;
  using native::wr16;
  const int mission = rd8(0x5ab9e);
  const uint32_t rec = kRouteTable + rd16(kRouteTable + 2);
  for (const ExtraSpawn& e : g_extras) {
    if (e.mission != 0 && e.mission != mission) continue;
    uint16_t saved[5];
    for (int i = 0; i < 5; ++i) saved[i] = rd16(rec + 2u * uint32_t(i));
    const int16_t pos[5] = {e.at.cx, e.at.cz, e.at.ox, e.at.oz, e.at.h};
    for (int i = 0; i < 5; ++i) wr16(rec + 2u * uint32_t(i), uint16_t(pos[i]));
    const uint16_t entry[5] = {e.spawn.tmpl, e.spawn.kind, e.spawn.slot, 2, e.spawn.flags};
    for (int i = 0; i < 5; ++i) wr16(g_entry + 2u * uint32_t(i), entry[i]);
    wr16(g_entry + 10, 0xFFFF);
    native::cpu().a[2] = g_entry;
    native::call_c(0x3d2ea);
    for (int i = 0; i < 5; ++i) wr16(rec + 2u * uint32_t(i), saved[i]);
  }
}

}  // namespace world
