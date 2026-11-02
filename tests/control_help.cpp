#include "control_help.hpp"

#include <cassert>
#include <string_view>

int main() {
  using namespace std::literals;
  // 6px glyphs and 8px margins on a 320px display.
  for (const auto& page : control_help::pages) {
    for (auto line : page) {
      assert(!line.empty());
      assert(line.size() <= (320 - 16) / 6);
    }
  }
  assert(control_help::pages[0][0] == "KEYBOARD / AMIGA CONTROLS"sv);
  assert(control_help::pages[1][0] == "GAME CONTROLLER"sv);
  // The routes whose labels were independently verified in Hunk 5.
  assert(control_help::pages[0][1].find("rudder") != std::string_view::npos);
  assert(control_help::pages[0][2].find("F1-F10") != std::string_view::npos);
  assert(control_help::pages[0][4].find("Space") != std::string_view::npos);
  assert(control_help::pages[0][8].find("radar") != std::string_view::npos);
}
