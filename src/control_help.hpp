#pragma once

// In-game control reference for the original 320x200 Amiga display.
// Key routes were cross-checked against retro-foundry's Hunk-5 dispatcher
// (docs/hunk5_input.md); gamepad bindings are the SDL host's own mapping.
#include <array>
#include <string_view>

namespace control_help {

inline constexpr int top = 8;
inline constexpr int row_height = 12;
inline constexpr int footer_y = 184;

inline constexpr std::array<std::array<std::string_view, 14>, 2> pages{{
    {{"KEYBOARD / AMIGA CONTROLS",
      "Arrows: pitch / roll    , . : rudder",
      "F1-F10: throttle   = / -: adjust",
      "F10 twice: afterburner",
      "Space: fire     Return: weapon",
      "G: gear    A: hook   Backspace: brakes",
      "H: HUD     J: ECM",
      "C: chaff   F: flares",
      "M: map     R: radar range",
      "T: next target    [ ]: zoom",
      "Keypad: look around  KP Enter: outside",
      "P: pause  Esc: back  Shift+Esc: select",
      "Insert/Ctrl+F9 menu  Ctrl+F11 gfx",
      "Ctrl+B blitter Ctrl+F10 mesh F11 full F12 mouse"}},
    {{"GAME CONTROLLER",
      "Left stick / D-pad: pitch and roll",
      "A: fire    B: select weapon",
      "X: next target    Y: map",
      "Start: pause    Back: debug menu",
      "LS click: brakes   RS click: gear",
      "RT/LT: throttle up / down",
      "Right stick X/Y: rudder / zoom",
      "LB+stick: systems compass menu",
      "N radar E target S map W HUD",
      "NE ECM SE chaff SW flare NW hook",
      "LB+LS click: gear",
      "LB+A/B/X/Y: HUD/ECM/chaff/flares",
      "RB+stick: views  RB+A: outside"}},
}};

static_assert(top + (int(pages[0].size()) - 1) * row_height + 12 <= footer_y,
              "help text must fit above the footer at 320x200");
static_assert(footer_y + 12 <= 200, "help footer must fit within 200 rows");

}  // namespace control_help
