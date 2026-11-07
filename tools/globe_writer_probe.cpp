// Headless original-code probe for the artificial-horizon globe.
// Build with tools/run_globe_writer_probe.sh; SDL, GL and native translations
// are intentionally absent. This does not replace any game code.
#include "amiga/machine.hpp"
#include "native/runtime.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// The interpreter's optional presentation/translation modules are not linked
// into this diagnostic executable. None of them is used by its flight run.
namespace recomp { const Image* image() { return nullptr; } }
namespace remaster {
void install(amiga::Machine&) {}
void reset() {}
bool compose(amiga::Machine&, std::vector<uint32_t>&) { return false; }
}
namespace scene {
bool enabled() { return false; }
bool software_render_enabled() { return false; }
bool compose_software(std::vector<uint32_t>&, int&, int&) { return false; }
}
namespace native {
[[noreturn]] void resume_main_loop() {
  throw std::runtime_error("headless probe does not support savestate continuation");
}
}

namespace {

struct Event {
  int frame = 0;
  std::string action, arg, mode;
};

std::vector<Event> read_script(const char* path) {
  std::ifstream file(path);
  if (!file) throw std::runtime_error(std::string("cannot read script: ") + path);
  std::vector<Event> events;
  std::string line;
  int previous = 0;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream in(line);
    std::string when;
    Event ev;
    if (!(in >> when >> ev.action)) continue;
    ev.frame = when[0] == '+' ? previous + std::stoi(when.substr(1)) : std::stoi(when);
    previous = ev.frame;
    in >> ev.arg >> ev.mode;
    if (ev.action == "key" || ev.action == "joy") events.push_back(std::move(ev));
  }
  std::stable_sort(events.begin(), events.end(), [](auto& a, auto& b) {
    return a.frame < b.frame;
  });
  return events;
}

struct ProbeHost final : amiga::Host {
  amiga::Machine* machine = nullptr;
  std::vector<Event> events;
  size_t next = 0;
  int limit = 0;
  int sample_every = 0;
  std::vector<int> map_frames{3500, 4500, 4700};
  std::vector<std::pair<int, uint8_t>> releases;
  std::array<bool, 5> joystick{};
  std::array<uint8_t, 41 * 48> previous{};
  bool have_previous = false;
  unsigned changed_samples = 0;
  int panel_anchor_frame = -1;
  std::array<uint8_t, 41 * 48> panel_anchor{};
  bool have_panel_anchor = false;

  void frame(const uint32_t*) override {
    const int frame = int(machine->frame_count());
    for (auto it = releases.begin(); it != releases.end();) {
      if (it->first <= frame) {
        machine->key_event(it->second, false);
        it = releases.erase(it);
      } else ++it;
    }
    while (next < events.size() && events[next].frame <= frame) {
      const Event& event = events[next++];
      if (event.action == "key") {
        // Match the front end's two named keys used in flight scenarios;
        // all other script keys use their already documented raw hex code.
        unsigned key = event.arg == "space" ? 0x40u :
                       event.arg == "return" ? 0x44u :
                       unsigned(std::stoul(event.arg, nullptr, 16));
        if (key > 0x7f) throw std::runtime_error("key outside Amiga rawkey range");
        if (event.mode == "up") machine->key_event(uint8_t(key), false);
        else {
          machine->key_event(uint8_t(key), true);
          if (event.mode != "down") releases.emplace_back(frame + 4, uint8_t(key));
        }
      } else if (event.action == "joy") {
        for (size_t i = 0; i < joystick.size(); ++i)
          joystick[i] = event.arg.find("udlrf"[i]) != std::string::npos;
      }
    }
    machine->joy_state(joystick[0], joystick[1], joystick[2], joystick[3], joystick[4]);

    // Explicitly selected map frames must be captured even when they fall
    // between the coarse periodic samples (for example, frame 4488 when
    // sample_every=20). A request for a map also emits its attitude sample.
    const bool wants_map = std::find(map_frames.begin(), map_frames.end(), frame) !=
                           map_frames.end();
    if ((sample_every > 0 && frame % sample_every == 0) || wants_map) {
      // Capture the entire observed globe window, including moving marks.
      // This is a displayed palette index, not a write to original RAM.
      const uint8_t* pixels = machine->frame_idx();
      if (pixels) {
        unsigned changed = 0, blue = 0, red = 0, other = 0;
        std::array<unsigned, 32> histogram{};
        size_t pos = 0;
        for (int y = 146; y < 194; ++y) {
          for (int x = 189; x < 230; ++x) {
            const uint8_t p = pixels[y * amiga::FB_W + x];
            if (have_previous && previous[pos] != p) ++changed;
            previous[pos++] = p;
            ++histogram[p & 31];
            blue += p == 5;
            red += p == 1;
            other += p != 5 && p != 1;
          }
        }
        have_previous = true;
        changed_samples += changed != 0;
        if (frame == panel_anchor_frame) {
          panel_anchor = previous;
          have_panel_anchor = true;
        } else if (have_panel_anchor && wants_map) {
          // A moving globe mask may actually be the entire original panel
          // moving. Compare *non-globe* pixels to a captured static bank,
          // allowing a +/-2 row displacement. Exclude the changing sphere
          // and a two-pixel guard, while keeping the sample count fixed.
          int best_dy = 0, best_errors = 0x7fffffff, base_errors = -1;
          int samples = 0;
          for (int dy = -2; dy <= 2; ++dy) {
            int errors = 0, count = 0;
            for (int y = 149; y <= 190; ++y) {
              for (int x = 189; x < 230; ++x) {
                if (x >= 195 && x <= 223 && y >= 160 && y <= 187)
                  continue;
                const int local_x = x - 189;
                const uint8_t expected = panel_anchor[(y - 146) * 41 + local_x];
                const uint8_t observed = pixels[(y + dy) * amiga::FB_W + x];
                errors += observed != expected;
                ++count;
              }
            }
            if (dy == 0) base_errors = errors;
            if (errors < best_errors) {
              best_errors = errors;
              best_dy = dy;
              samples = count;
            }
          }
          std::printf("globe-panel-alignment: frame=%d anchor=%d shift-y=%+d "
                      "errors=%d baseline-errors=%d samples=%d\n",
                      frame, panel_anchor_frame, best_dy, best_errors,
                      base_errors, samples);
        }
        int horizon_row = -1;
        // Estimate the split only when enough original globe fill is visible.
        // For roll this majority-row estimate becomes ambiguous; raw pen
        // counts and the full pixel mask remain the primary observations.
        if (red + blue > 300) {
          int last_blue = 162;
          for (int y = 163; y < 184; ++y) {
            unsigned r = 0, b = 0;
            for (int x = 198; x < 221; ++x) {
              r += pixels[y * amiga::FB_W + x] == 1;
              b += pixels[y * amiga::FB_W + x] == 5;
            }
            if (b > r) last_blue = y;
          }
          horizon_row = last_blue + 1;
        }
        const int16_t player = int16_t(machine->r16(0x5abd6));
        if (player >= 0 && player < 8192 && player % 512 == 0) {
          uint32_t base = 0x5b47c + unsigned(player);
          std::printf("globe-sample: frame=%d rowkey=%06x player=%d "
                      "a102=%u a104=%u a106=%u hrow=%d blue5=%u red1=%u other=%u diff=%u "
                      "rgb1=%06x rgb5=%06x pens=",
                      frame, machine->frame_row_key()[170], player / 512,
                      machine->r16(base + 102), machine->r16(base + 104),
                      machine->r16(base + 106), horizon_row, blue, red, other, changed,
                      machine->frame_row_pal()[170 * 32 + 1],
                      machine->frame_row_pal()[170 * 32 + 5]);
          bool comma = false;
          for (unsigned pen = 0; pen < histogram.size(); ++pen) {
            if (!histogram[pen]) continue;
            std::printf("%s%u:%u", comma ? "," : "", pen, histogram[pen]);
            comma = true;
          }
          std::putchar('\n');
          if (wants_map) {
            // $39b06's scripted view sequence writes $5ac10. The generic
            // panel blitter at $44c42 later consumes that signed offset.
            // Record the event mode and cursor beside the pixel evidence.
            const uint32_t record = 0x5b47c + uint32_t(uint16_t(player));
            std::printf("globe-view-sequence: frame=%d mode=%u tick=%u "
                        "forward=%d view=%d flags=%04x state=%04x "
                        "altitude=%d speed=%d\n",
                        frame, machine->r8(0x5aab8), machine->r8(0x5ab87),
                        int32_t(machine->r32(0x5ac10)),
                        int16_t(machine->r16(0x5ac7c)),
                        machine->r16(0x5abca), machine->r16(record + 2),
                        int32_t(machine->r32(record + 24)),
                        int16_t(machine->r16(record + 110)));
            for (int y = 163; y < 184; ++y) {
              std::printf("globe-map: frame=%d y=%d ", frame, y);
              for (int x = 198; x < 221; ++x) {
                const uint8_t p = pixels[y * amiga::FB_W + x];
                std::putchar(p == 1 ? 'R' : p == 5 ? 'B' : '.');
              }
              std::putchar('\n');
            }
          }
          std::fflush(stdout);
        }
      }
    }
    if (frame >= limit) throw amiga::QuitRequested{};
  }

  void audio(const int16_t*, size_t) override {}
};

struct SourceWriteWatch {
  amiga::Machine* machine = nullptr;
  int first = 0, last = 0, limit = 0, printed = 0;
  int bitmap_first = 0, bitmap_last = 0, bitmap_limit = 0, bitmap_printed = 0;
  int tuple_first = 0, tuple_last = 0, tuple_limit = 0, tuple_printed = 0;

  static void observe(void* opaque, uint32_t address, uint8_t width, bool write) {
    auto& watch = *static_cast<SourceWriteWatch*>(opaque);
    if (!write) return;
    const int frame = int(watch.machine->frame_count());
    const bool descriptor = address < 0x5a9d0 && address + width > 0x5a960 &&
                            frame >= watch.first && frame <= watch.last &&
                            watch.printed < watch.limit;
    const bool bitmap = address < 0x74000 && address + width > 0x73600 &&
                        frame >= watch.bitmap_first && frame <= watch.bitmap_last &&
                        watch.bitmap_printed < watch.bitmap_limit;
    const bool tuple = address < 0x608f0 && address + width > 0x60684 &&
                       frame >= watch.tuple_first && frame <= watch.tuple_last &&
                       watch.tuple_printed < watch.tuple_limit;
    if (!descriptor && !bitmap && !tuple) return;
    if (descriptor) ++watch.printed;
    else if (bitmap) ++watch.bitmap_printed;
    else ++watch.tuple_printed;
    std::printf("%s: frame=%d pc=%06x address=%06x width=%u value=",
                descriptor ? "globe-source-write" :
                bitmap ? "globe-bitmap-write" : "globe-tuple-write",
                frame, watch.machine->cpu.op_pc, address, width);
    // Read the RAM bytes directly: calling Machine::r8 here would recurse
    // into this observer. The bus write has already occurred.
    for (unsigned i = 0; i < width; ++i)
      std::printf("%02x", watch.machine->ram[address + i]);
    std::putchar('\n');
  }
};

// Read-only snapshots around the original game_frame cockpit calls. The
// low-level $44c42 rectangle blit copies an already-prepared panel, so its
// BLTSIZE sites alone cannot identify the attitude-image producer. Compare
// the two source banks *before* each higher-level draw call to locate the
// routine that actually changes the 4-bit globe fill.
struct GlobeStageWatch {
  amiga::Machine* machine = nullptr;
  int first = 0, last = 0, limit = 0, printed = 0;
  bool instructions = false;
  bool maps = false;
  static constexpr int x0 = 192, x_bytes = 4, y0 = 163, height = 21;
  static constexpr int planes = 4, banks = 2;
  static constexpr size_t bytes = banks * planes * height * x_bytes;
  std::array<uint8_t, bytes> previous{};
  bool have_previous = false;
  uint32_t previous_pc = 0;
  int previous_frame = -1;

  uint32_t read32(uint32_t addr) const {
    const uint8_t* ram = machine->ram;
    return (uint32_t(ram[addr]) << 24) | (uint32_t(ram[addr + 1]) << 16) |
           (uint32_t(ram[addr + 2]) << 8) | ram[addr + 3];
  }

  void observe(uint32_t pc) {
    const int frame = int(machine->frame_count());
    if (frame < first || frame > last || printed >= limit) return;
    std::array<uint8_t, bytes> now{};
    std::array<uint32_t, banks> bases{};
    for (int bank = 0; bank < banks; ++bank) {
      const uint32_t table = bank ? 0x5a976u : 0x5a966u;
      const uint32_t base = read32(table + 12) & 0x00ffffffu;
      if (!base || base > amiga::RAM_SIZE - 32000) return;
      bases[bank] = base;
      for (int plane = 0; plane < planes; ++plane)
        for (int y = 0; y < height; ++y)
          for (int x = 0; x < x_bytes; ++x) {
            const size_t i = (((size_t(bank) * planes + plane) * height + y) * x_bytes + x);
            now[i] = machine->ram[base + uint32_t(plane) * 8000 +
                                  uint32_t(y0 + y) * 40 + x0 / 8 + uint32_t(x)];
          }
    }
    std::array<unsigned, banks> changed{};
    if (have_previous)
      for (size_t i = 0; i < bytes; ++i)
        changed[i / (bytes / banks)] += std::popcount(unsigned(now[i] ^ previous[i]));
    // Record the first entry of every observed frame and any changed bank.
    // The change belongs to instructions *between* the previous hook and
    // this one, not necessarily to the present call.
    if (!have_previous || frame != previous_frame || changed[0] || changed[1]) {
      std::printf("globe-stage: frame=%d after=%06x before=%06x "
                  "bank0=%06x bits0=%u bank1=%06x bits1=%u",
                  frame, previous_pc, pc, bases[0], changed[0], bases[1], changed[1]);
      if (instructions) {
        const auto& cpu = machine->cpu;
        std::printf(" d0=%08x d1=%08x d2=%08x d3=%08x "
                    "a0=%06x a1=%06x a2=%06x a3=%06x",
                    cpu.d[0], cpu.d[1], cpu.d[2], cpu.d[3],
                    cpu.a[0], cpu.a[1], cpu.a[2], cpu.a[3]);
      }
      std::putchar('\n');
      ++printed;
    }
    previous = now;
    have_previous = true;
    previous_pc = pc;
    previous_frame = frame;
    if (maps && (pc == 0x455bau || pc == 0x44c42u || pc == 0x474acu)) {
      // Dump the original four-plane pen map at each stage so the dynamic
      // image can be compared with the pristine ADF mask after resets.
      for (int bank = 0; bank < banks; ++bank) {
        for (int y = 163; y < 184; ++y) {
          std::printf("globe-stage-map: frame=%d before=%06x bank=%d y=%d ",
                      frame, pc, bank, y);
          for (int x = 198; x <= 220; ++x) {
            const uint32_t off = uint32_t(y) * 40 + uint32_t(x / 8);
            uint8_t pen = 0;
            for (int p = 0; p < planes; ++p)
              pen |= uint8_t(((machine->ram[bases[bank] + uint32_t(p) * 8000 + off]
                                >> (7 - (x & 7))) & 1) << p);
            std::putchar(pen == 1 ? 'R' : pen == 5 ? 'B' : '.');
          }
          std::putchar('\n');
        }
      }
    }
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3 || argc > 5) {
    std::fprintf(stderr, "usage: globe_writer_probe ORIGINAL.adf SCRIPT [frames=6000] [sample-every=100]\n");
    return 2;
  }
  try {
    ProbeHost host;
    host.events = read_script(argv[2]);
    host.limit = argc >= 4 ? std::stoi(argv[3]) : 6000;
    host.sample_every = argc >= 5 ? std::stoi(argv[4]) : 100;
    if (const char* frame = std::getenv("FA18_GLOBE_PANEL_ANCHOR"))
      host.panel_anchor_frame = std::stoi(frame);
    if (const char* selected = std::getenv("FA18_GLOBE_MAP_FRAMES")) {
      host.map_frames.clear();
      std::istringstream text(selected);
      std::string item;
      while (std::getline(text, item, ',')) {
        if (!item.empty()) host.map_frames.push_back(std::stoi(item));
      }
    }
    amiga::Machine machine;
    host.machine = &machine;
    machine.native_mode = amiga::Machine::NativeMode::Off;
    machine.use_recomp = false;
    machine.set_det_timing(true);
    // Allocate palette-index and copper-row captures without enabling a
    // remaster renderer. The CPU still uses only the original 68k interpreter.
    machine.hires_scale = 1;
    machine.set_host(&host);
    if (!machine.boot(argv[1])) return 1;
    SourceWriteWatch source_watch;
    // Locate the producer of the per-bank source pointer words A2=$5a966
    // and A2=$5a976. This observer does not write guest RAM or alter timing.
    // FA18_GLOBE_SOURCE_WATCH=4420:4500:128  (first:last:maximum writes)
    bool track_writes = false;
    if (const char* config = std::getenv("FA18_GLOBE_SOURCE_WATCH")) {
      if (std::sscanf(config, "%d:%d:%d", &source_watch.first,
                      &source_watch.last, &source_watch.limit) != 3 ||
          source_watch.first < 0 || source_watch.last < source_watch.first ||
          source_watch.limit < 0)
        throw std::runtime_error("expected FA18_GLOBE_SOURCE_WATCH=first:last:limit");
      track_writes = true;
    }
    // Independently inspect the mutable image read by BLTAPTR at $451ac.
    // The selected descending rectangles reach below $73800, so this watch
    // covers $73600..$74000 (one 2560-byte source neighborhood).
    // FA18_GLOBE_BITMAP_WATCH=4476:4480:80
    if (const char* config = std::getenv("FA18_GLOBE_BITMAP_WATCH")) {
      if (std::sscanf(config, "%d:%d:%d", &source_watch.bitmap_first,
                      &source_watch.bitmap_last, &source_watch.bitmap_limit) != 3 ||
          source_watch.bitmap_first < 0 ||
          source_watch.bitmap_last < source_watch.bitmap_first ||
          source_watch.bitmap_limit < 0)
        throw std::runtime_error("expected FA18_GLOBE_BITMAP_WATCH=first:last:limit");
      track_writes = true;
    }
    // The Hunk-36 raster worker reads a count-prefixed XY tuple array at
    // $60684. Find the *writer* of this array, before its line/area blits.
    // FA18_GLOBE_TUPLE_WATCH=4476:4480:256
    if (const char* config = std::getenv("FA18_GLOBE_TUPLE_WATCH")) {
      if (std::sscanf(config, "%d:%d:%d", &source_watch.tuple_first,
                      &source_watch.tuple_last, &source_watch.tuple_limit) != 3 ||
          source_watch.tuple_first < 0 ||
          source_watch.tuple_last < source_watch.tuple_first ||
          source_watch.tuple_limit < 0)
        throw std::runtime_error("expected FA18_GLOBE_TUPLE_WATCH=first:last:limit");
      track_writes = true;
    }
    if (track_writes) {
      source_watch.machine = &machine;
      machine.ram_access_context = &source_watch;
      machine.ram_access_observer = &SourceWriteWatch::observe;
    }
    // The 68000 copies a projected sea/horizon polygon from $60684 to
    // $60726 using one MOVE.W and five MOVE.L instructions at $44b36.
    // Observe both sides at instruction boundaries to establish the source
    // of the list later read by $44da4, without tracing every tuple write.
    // FA18_GLOBE_POLY_TRACE=4476:4480:16
    if (const char* config = std::getenv("FA18_GLOBE_POLY_TRACE")) {
      int first = 0, last = 0, limit = 0;
      if (std::sscanf(config, "%d:%d:%d", &first, &last, &limit) != 3 ||
          first < 0 || last < first || limit < 0)
        throw std::runtime_error("expected FA18_GLOBE_POLY_TRACE=first:last:limit");
      auto printed = std::make_shared<int>(0);
      for (uint32_t pc : {0x44b36u, 0x44b42u, 0x44de4u}) {
        machine.add_pc_hook(pc, [&, pc, first, last, limit, printed] {
          const int frame = int(machine.frame_count());
          if (frame < first || frame > last || *printed >= limit) return;
          if (pc == 0x44de4) {
            // The 2D raster worker has other callers too. Attribute this
            // transformed polygon to the globe pass from game_frame.
            bool globe_caller = false;
            for (int i = 0; i < 7; ++i) {
              const uint32_t a = machine.cpu.a[7] + uint32_t(4 * i);
              if (a < amiga::RAM_SIZE - 4 && machine.r32(a) == 0x213ae) {
                globe_caller = true;
                break;
              }
            }
            if (!globe_caller) return;
          }
          ++*printed;
          const uint32_t address = pc == 0x44b42 ? 0x60726 : 0x60684;
          const char* kind = pc == 0x44b36 ? "source" :
                             pc == 0x44b42 ? "copy" : "scaled";
          std::printf("globe-poly-%s: frame=%d pc=%06x source=%06x "
                      "count=%u words=",
                      kind, frame, pc, address, machine.r16(address));
          for (int i = 0; i < 11; ++i)
            std::printf("%s%04x", i ? "," : "", machine.r16(address + uint32_t(i * 2)));
          std::putchar('\n');
          if (pc == 0x44de4) {
            std::printf("globe-poly-offsets: frame=%d x=%d y=%d\n",
                        frame, int16_t(machine.r16(0x5ac80)),
                        int16_t(machine.r16(0x5abd0)));
            const uint32_t descriptor = machine.cpu.a[2] & 0x00ffffffu;
            if (descriptor == 0x5a966 || descriptor == 0x5a976) {
              const uint32_t base = machine.r32(descriptor + 12) & 0x00ffffffu;
              std::printf("globe-poly-bank: frame=%d desc=%06x base=%06x "
                          "display=%06x\n", frame, descriptor, base,
                          machine.frame_row_key()[170] - 170u * 40u);
            }
          } else if (pc == 0x44b36) {
            // $422f8 builds the cockpit attitude matrix $5aee2 from
            // player-record +102/+104/+106. Snapshot the matrix actually
            // used by $42f62 in this *same code frame*, independently of
            // the later host/display-frame attitude sample.
            const uint32_t player = 0x5b47c + machine.r16(0x5abd6);
            std::printf("globe-poly-attitude: frame=%d pitch=%d heading=%d "
                        "bank=%d matrix=",
                        frame, int16_t(machine.r16(player + 102)),
                        int16_t(machine.r16(player + 104)),
                        int16_t(machine.r16(player + 106)));
            for (int i = 0; i < 9; ++i)
              std::printf("%s%04x", i ? "," : "",
                          machine.r16(0x5aee2 + uint32_t(i * 2)));
            std::putchar('\n');
          }
        });
      }
    }
    // FA18_GLOBE_STAGE_TRACE=4472:4496:160 (first:last:max snapshots).
    // Compare live *original* source-bank pixels between the cockpit call
    // boundaries, without tracing every 68000 instruction or touching RAM.
    GlobeStageWatch stage_watch;
    if (const char* config = std::getenv("FA18_GLOBE_STAGE_TRACE")) {
      if (std::sscanf(config, "%d:%d:%d", &stage_watch.first,
                      &stage_watch.last, &stage_watch.limit) != 3 ||
          stage_watch.first < 0 || stage_watch.last < stage_watch.first ||
          stage_watch.limit < 0)
        throw std::runtime_error("expected FA18_GLOBE_STAGE_TRACE=first:last:limit");
      stage_watch.machine = &machine;
      stage_watch.instructions = std::getenv("FA18_GLOBE_STAGE_INSNS") != nullptr;
      stage_watch.maps = std::getenv("FA18_GLOBE_STAGE_MAPS") != nullptr;
      for (uint32_t pc : {0x45368u, 0x455bau, 0x45e2au, 0x47ec0u,
                          0x45d2eu, 0x45b7cu, 0x46abau, 0x46b50u,
                          0x46d7cu, 0x46c1eu, 0x46d2eu, 0x4551cu,
                          0x44c42u, 0x474acu, 0x45604u, 0x46dd6u,
                          0x46e64u, 0x466d0u, 0x45938u, 0x46668u,
                          0x46ef2u, 0x45760u}) {
        machine.add_pc_hook(pc, [&, pc] { stage_watch.observe(pc); });
      }
      if (stage_watch.instructions) {
        // Dense read-only instruction boundary probes, covering the
        // candidate attitude producer. The hook only runs if the address
        // is a real instruction PC, and is never installed in normal runs.
        for (uint32_t pc = 0x4536au; pc < 0x454dcu; pc += 2)
          machine.add_pc_hook(pc, [&, pc] { stage_watch.observe(pc); });
      }
    }
    // Read-only PC-entry snapshots locate the *callers* of the low-level
    // BLTSIZE writers. This is optional: the ordinary pixel replay doesn't
    // need per-instruction instrumentation or a heavyweight instruction log.
    // FA18_GLOBE_WRITER_REGS=4470:4490:80  (first:last:maximum snapshots)
    if (const char* config = std::getenv("FA18_GLOBE_WRITER_REGS")) {
      int first = 0, last = 0, limit = 64;
      if (std::sscanf(config, "%d:%d:%d", &first, &last, &limit) < 2 ||
          first < 0 || last < first || limit < 0)
        throw std::runtime_error("expected FA18_GLOBE_WRITER_REGS=first:last[:limit]");
      auto snapshots = std::make_shared<int>(0);
      for (uint32_t pc : {0x44c42u, 0x45ce6u, 0x44d08u, 0x451acu}) {
        machine.add_pc_hook(pc, [&, pc, first, last, limit, snapshots] {
          // Machine::add_pc_hook replaces an earlier hook at the same PC.
          // Preserve the call-boundary capture when both diagnostics are
          // enabled by invoking it from the register snapshot hook.
          if (pc == 0x44c42 && std::getenv("FA18_GLOBE_STAGE_TRACE"))
            stage_watch.observe(pc);
          const int frame = int(machine.frame_count());
          if (frame < first || frame > last || *snapshots >= limit) return;
          const auto& cpu = machine.cpu;
          // $45ce6 is a generic clipping helper invoked by many drawing
          // routines. Only its $44c80 caller belongs to the globe pass.
          if (pc == 0x45ce6 && machine.r32(cpu.a[7]) != 0x44c86) return;
          ++*snapshots;
          std::printf("globe-writer-regs: frame=%d pc=%06x sp=%06x fp=%06x"
                      " d0=%08x d1=%08x d2=%08x d3=%08x"
                      " d4=%08x d5=%08x d6=%08x d7=%08x"
                      " a0=%08x a1=%08x a2=%08x"
                      " view=%u forward=%08x stack=",
                      frame, pc, cpu.a[7], cpu.a[6], cpu.d[0], cpu.d[1],
                      cpu.d[2], cpu.d[3], cpu.d[4], cpu.d[5], cpu.d[6],
                      cpu.d[7], cpu.a[0], cpu.a[1], cpu.a[2],
                      machine.r16(0x5ac7e), machine.r32(0x5ac10));
          for (unsigned i = 0; i < 8; ++i) {
            const uint32_t addr = cpu.a[7] + 4 * i;
            std::printf("%s%08x", i ? "," : "", addr < amiga::RAM_SIZE - 4
                                          ? machine.r32(addr) : 0xffffffffu);
          }
          std::putchar('\n');
          if (pc == 0x44d08 || pc == 0x451ac) {
            // The original blitter pointer registers are not readable
            // through the Amiga's normal Custom-chip read bus. Snapshot the
            // emulated write-side latches immediately before BLTSIZE fires.
            // This distinguishes a precomputed source image from the live
            // drawing-bank destination without modifying guest memory.
            const auto blt = machine.blitter_registers();
            std::printf("globe-blitter-input: frame=%d pc=%06x "
                        "con0=%04x con1=%04x a=%06x b=%06x c=%06x d=%06x "
                        "amod=%d bmod=%d cmod=%d dmod=%d\n",
                        frame, pc, blt.con0, blt.con1,
                        blt.ptr[0], blt.ptr[1], blt.ptr[2], blt.ptr[3],
                        blt.mod[0], blt.mod[1], blt.mod[2], blt.mod[3]);
            if (pc == 0x451ac) {
              // This BLTSIZE instruction uses D0 as its source-rectangle
              // height/width. Fingerprint both original input bitmaps at
              // the precise pre-blit boundary (without dumping game art).
              const int rows = int((cpu.d[0] & 0xffffu) >> 6);
              const int words = int(cpu.d[0] & 63u);
              const bool descending = (blt.con1 & 2u) != 0;
              auto source_hash = [&](int channel) -> uint32_t {
                uint32_t hash = 2166136261u;
                uint32_t addr = blt.ptr[channel];
                for (int y = 0; y < rows; ++y) {
                  for (int i = 0; i < words; ++i) {
                    if (addr >= amiga::RAM_SIZE - 1) return 0;
                    hash = (hash ^ machine.ram[addr]) * 16777619u;
                    hash = (hash ^ machine.ram[addr + 1]) * 16777619u;
                    addr += uint32_t(descending ? -2 : 2);
                  }
                  addr += uint32_t(descending ? -blt.mod[channel] : blt.mod[channel]);
                }
                return hash;
              };
              std::printf("globe-blitter-sources: frame=%d pc=%06x "
                          "rows=%d words=%d a-hash=%08x c-hash=%08x\n",
                          frame, pc, rows, words,
                          source_hash(0), source_hash(2));
              // Probe the *same absolute* surrounding source-image RAM in
              // level and banked replays. A selected source-slice hash can
              // differ solely because its pointer/height moved; the fixed
              // region hash answers whether the bitmap was rewritten.
              uint32_t image_hash = 2166136261u;
              for (uint32_t addr = 0x73800; addr < 0x74000; ++addr)
                image_hash = (image_hash ^ machine.ram[addr]) * 16777619u;
              std::printf("globe-blitter-region: frame=%d "
                          "start=073800 end=074000 hash=%08x\n",
                          frame, image_hash);
            }
          }
        });
      }
    }
    // Differential instruction trace of the generic $44da4..$451ac globe
    // blitter parameter preparation. This is deliberately separate from the
    // writer-register hooks (add_pc_hook replaces entries at the same PC).
    // FA18_GLOBE_CALC_TRACE=4480:4480:250
    if (const char* config = std::getenv("FA18_GLOBE_CALC_TRACE")) {
      int first = 0, last = 0, limit = 0;
      if (std::sscanf(config, "%d:%d:%d", &first, &last, &limit) != 3 ||
          first < 0 || last < first || limit < 0)
        throw std::runtime_error("expected FA18_GLOBE_CALC_TRACE=first:last:limit");
      auto printed = std::make_shared<int>(0);
      for (uint32_t pc = 0x44da4; pc <= 0x451ac; pc += 2) {
        machine.add_pc_hook(pc, [&, pc, first, last, limit, printed] {
          const int frame = int(machine.frame_count());
          if (frame < first || frame > last || *printed >= limit) return;
          const auto& cpu = machine.cpu;
          // These generic raster helpers also run hundreds of times while
          // painting the 3D scene. Attribute only the nested call made by
          // game_frame at $213a8, whose return PC is $213ae.
          bool globe_caller = false;
          for (int i = 0; i < 7; ++i) {
            const uint32_t address = cpu.a[7] + uint32_t(i * 4);
            if (address < amiga::RAM_SIZE - 4 && machine.r32(address) == 0x213ae) {
              globe_caller = true;
              break;
            }
          }
          if (!globe_caller) return;
          ++*printed;
          std::printf("globe-calc-insn: frame=%d pc=%06x op=%04x "
                      "d0=%08x d1=%08x d2=%08x d3=%08x "
                      "d4=%08x d5=%08x d6=%08x "
                      "a0=%06x a1=%06x a2=%06x a3=%06x\n",
                      frame, pc, machine.r16(pc),
                      cpu.d[0], cpu.d[1], cpu.d[2], cpu.d[3],
                      cpu.d[4], cpu.d[5], cpu.d[6],
                      cpu.a[0], cpu.a[1], cpu.a[2], cpu.a[3]);
        });
      }
    }
    int result = machine.run();
    std::fprintf(stderr, "globe-probe: frames=%llu changed_samples=%u\n",
                 (unsigned long long)machine.frame_count(), host.changed_samples);
    return result;
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "globe-probe: %s\n", ex.what());
    return 1;
  }
}
