// World database of the 3D scene: where the static ground objects
// (buildings, runways, islands, lights, ...) and the mission objects
// (aircraft, ships) are placed.  Format documented in docs/world_format.md.
//
// The data is decoded from the original executable (or from the live memory
// image) into `World`, written as a text "world file" and read back; apply()
// writes a (possibly edited) world into the machine's memory so the original
// scene builder ($30064) and mission setup ($3cf0e) use it.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace amiga { struct Machine; }

namespace world {

// ---- static object maps (hunk 66) ------------------------------------------
//
// Three grid maps, rows = Z, columns = X; a row lists its cells by column.
//   Near   ($5778c): 128x128 cells (1 cell = 2^22 world units)
//   FarA   ($5758c): sectors of 4x4 cells (2^24 world units)
//   FarB   ($5768c): like FarA, second far list
// Coordinates in the text file are in "units" of 256 world units (the scale
// of scenario record +12/+14): a cell is 16384 units, a sector 65536.
enum MapId { Near = 0, FarA = 1, FarB = 2 };
constexpr int kMaps = 3;
constexpr uint32_t kMapBase[kMaps] = {0x5778c, 0x5758c, 0x5768c};
constexpr const char* kMapName[kMaps] = {"near", "farA", "farB"};
constexpr int kMapRows = 128;  // row offset arrays are 128 words for every map

struct Object {
  uint8_t subcell = 0;   // 0..15: (3 - sx) + 4 * (3 - sz), sx/sz the quarter of the cell
  uint8_t type = 0;      // type table $36094 index (16..127; 0..15 are scenario slots)
  bool compact = false;  // position from the 16-entry table $3080e (index `pos`)
  uint8_t pos = 0;       // compact: table index (high nibble of the first byte)
  int16_t x = 0, z = 0;  // explicit: position inside the subcell (near: units,
                         // far: units / 4), normally 0..4095
};

struct Cell {
  int x = 0, z = 0;              // grid column / row
  std::vector<Object> objects;   // grouped by subcell (draw order inside a group)
};

struct Map {
  std::vector<Cell> cells;       // sorted by (z, x); empty cells are kept (they mark the cell)
};

// Absolute position (units) of an object in a map, and the inverse.
void object_units(MapId m, const Cell& c, const Object& o, int32_t& X, int32_t& Z);
bool object_from_units(MapId m, int cx, int cz, int32_t X, int32_t Z, Object& o, std::string& err);

// ---- section scripts (hunk 65) ----------------------------------------------
//
// Potentially visible neighbour cells (and their subcells) relative to the
// camera, in drawing order (back to front), per camera position class and
// view heading octant.  Neighbour n is an index into $306bc (dx, dz).
struct ScriptStep {
  uint8_t neighbour = 0;
  std::vector<uint8_t> subcells;
};
struct Script {
  std::vector<ScriptStep> steps;
};
struct ScriptTable {
  uint32_t base = 0;             // $5654a / $567ac / $5738e
  std::vector<Script> scripts;   // 4 / 32 / 64 entries
};
constexpr uint32_t kScriptBase[3] = {0x5654a, 0x567ac, 0x5738e};
constexpr int kScriptCount[3] = {4, 32, 64};
constexpr uint32_t kScriptEnd = 0x57500;  // end of hunk-65 script data

// ---- mission objects (hunk 27, patched in place) ---------------------------
//
// Spawn entry (10 bytes), consumed by $3d320: creates scenario record `slot`
// from template `tmpl` of $35f54.
struct Spawn {
  uint16_t tmpl = 0;   // byte offset into $35f54 (template index * 20)
  uint16_t kind = 0;   // low byte -> record +98 (class in the high nibble)
  uint16_t slot = 0;   // bits 0-6 slot (record index), bits 8-14 -> record +58 (route)
  uint16_t pos = 0;    // byte offset into the route table $3ddc8 (route index * 2)
  uint16_t flags = 0;  // see docs/world_format.md
};
struct SpawnList {
  uint32_t addr = 0;            // runtime address of the first entry
  std::vector<Spawn> spawns;    // terminated by a negative word in memory
  std::string users;            // "m1v0l0 ..." (informational)
};
struct Waypoint {
  int16_t cx = 0, cz = 0;       // cell
  int16_t ox = 0, oz = 0;       // offset in the cell (units)
  int16_t h = 0;                // height (units)
};
// Position table $3ddc8: 60 word offsets (index = byte offset / 2) to
// 10-byte waypoint records.  A spawn's start position is the record at its
// `pos` index; a route (record +58) continues with the following records
// until one starts with a negative word.
struct WaypointRec {
  uint16_t offset = 0;          // from $3ddc8
  Waypoint wp;
};
struct Zone {
  uint32_t addr = 0;
  int16_t x1 = 0, x2 = 0, z1 = 0, z2 = 0;  // player cell bounds (inclusive)
  std::vector<Spawn> spawns;
};
constexpr uint32_t kRouteTable = 0x3ddc8;
constexpr uint32_t kZoneTable = 0x3df08;
constexpr uint32_t kMissionTable = 0x3dfba;
constexpr int kPositions = 60;
constexpr uint32_t kHunk27End = 0x3e6e0;
constexpr int kMissions = 10;     // 1..9, 10 = missions 126/127
constexpr int kVariants = 3;
constexpr int kLevels = 4;

struct MissionEntry {
  int mission = 0, variant = 0;
  uint32_t list = 0;            // list header (4 level offsets)
  int16_t max_count = 0;        // -> $5aba2
  uint16_t level_off[kLevels] = {};
};

// Template (type table layout, read-only information).
struct Template {
  uint32_t draw = 0, model = 0, lod = 0, lod2 = 0, lod3 = 0;
};

struct World {
  // Parts not present in a world file keep the original data.
  bool has_map[kMaps] = {};
  bool has_scripts[3] = {};
  Map maps[kMaps];
  ScriptTable scripts[3];
  std::vector<MissionEntry> missions;
  std::vector<SpawnList> spawn_lists;
  std::vector<uint16_t> positions;      // the 60 offsets of $3ddc8 (0 = unused)
  std::vector<WaypointRec> waypoints;   // sorted by offset
  std::vector<Zone> zones;
  std::vector<Template> templates;   // 16 entries of $35f54
};

// Byte reader at runtime addresses (loader layout, LOAD_BASE = $20000).
using Reader = std::function<uint8_t(uint32_t)>;

bool decode(const Reader& rd, World& w, std::string& err);
// Relocate the hunk executable like Machine::boot and decode it.
bool decode_exe(const std::vector<uint8_t>& exe, World& w, std::string& err);

std::string to_text(const World& w);
bool from_text(const std::string& text, World& w, std::string& err);
bool load_file(const std::string& path, World& w, std::string& err);

// Validate against the limits of the original code (see the docs).
bool validate(const World& w, const Reader& rd, std::string& err);

// Write the world into memory: map rows into hunk 66 after the row offset
// arrays, cell lists into a block reserved at the top of RAM, scripts into
// hunk 65, mission data in place (same entry counts as the original).
// Call after Machine::boot() and before the game runs.
bool apply(amiga::Machine& m, const World& w, std::string& err);

}  // namespace world
