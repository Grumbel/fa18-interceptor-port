// Headless original-code probe for the artificial-horizon globe.
// Build with tools/run_globe_writer_probe.sh; SDL, GL and native translations
// are intentionally absent. This does not replace any game code.
#include "amiga/machine.hpp"
#include "native/runtime.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
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

  static void observe(void* opaque, uint32_t address, uint8_t width, bool write) {
    auto& watch = *static_cast<SourceWriteWatch*>(opaque);
    if (!write || address >= 0x5a9d0 || address + width <= 0x5a960 ||
        watch.printed >= watch.limit) return;
    const int frame = int(watch.machine->frame_count());
    if (frame < watch.first || frame > watch.last) return;
    ++watch.printed;
    std::printf("globe-source-write: frame=%d pc=%06x address=%06x width=%u value=",
                frame, watch.machine->cpu.op_pc, address, width);
    // Read the RAM bytes directly: calling Machine::r8 here would recurse
    // into this observer. The bus write has already occurred.
    for (unsigned i = 0; i < width; ++i)
      std::printf("%02x", watch.machine->ram[address + i]);
    std::putchar('\n');
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
    if (const char* config = std::getenv("FA18_GLOBE_SOURCE_WATCH")) {
      if (std::sscanf(config, "%d:%d:%d", &source_watch.first,
                      &source_watch.last, &source_watch.limit) != 3 ||
          source_watch.first < 0 || source_watch.last < source_watch.first ||
          source_watch.limit < 0)
        throw std::runtime_error("expected FA18_GLOBE_SOURCE_WATCH=first:last:limit");
      source_watch.machine = &machine;
      machine.ram_access_context = &source_watch;
      machine.ram_access_observer = &SourceWriteWatch::observe;
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
      int snapshots = 0;
      for (uint32_t pc : {0x44c42u, 0x45ce6u, 0x44d08u, 0x451acu}) {
        machine.add_pc_hook(pc, [&, pc] {
          const int frame = int(machine.frame_count());
          if (frame < first || frame > last || snapshots >= limit) return;
          const auto& cpu = machine.cpu;
          // $45ce6 is a generic clipping helper invoked by many drawing
          // routines. Only its $44c80 caller belongs to the globe pass.
          if (pc == 0x45ce6 && machine.r32(cpu.a[7]) != 0x44c86) return;
          ++snapshots;
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
