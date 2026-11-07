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

    if (sample_every > 0 && frame % sample_every == 0) {
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
          if (std::find(map_frames.begin(), map_frames.end(), frame) != map_frames.end()) {
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
    int result = machine.run();
    std::fprintf(stderr, "globe-probe: frames=%llu changed_samples=%u\n",
                 (unsigned long long)machine.frame_count(), host.changed_samples);
    return result;
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "globe-probe: %s\n", ex.what());
    return 1;
  }
}
