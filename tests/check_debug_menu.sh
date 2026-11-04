#!/usr/bin/env bash
# Verify the actual in-game menu pages and original game-font rendering with
# a tiny deterministic scripted replay. No SDL window interaction required.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "usage: $0 ORIGINAL.adf [fa18-executable]" >&2
  exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
for page in main graphics controls extras; do
  shot="$tmp/$page.ppm"
  log="$tmp/$page.log"
  nav=''
  case "$page" in
    graphics) nav='down right' ;;
    controls) nav='down down enter' ;;
    extras) nav='down down down down down right' ;;
  esac
  FA18_MENU_NAV="$nav" FA18_MENU_TRACE=1 FA18_MENU_SHOT="$shot" \
    "$bin" --native on --fast --frames 5 \
    --script "$repo/tests/data/debug_menu_pages.txt" "$adf" >"$log" 2>&1
  test -s "$shot"
  case "$page" in
    main) grep -q 'menu: DEBUG MENU | Resume | Graphics > | Controls > | Save states > | Load states > | Extras >' "$log" ;;
    graphics) grep -q 'menu: GRAPHICS | Back | 3D renderer: .* | Widescreen: off | Render FPS: 60 (OpenGL) | Full detail:' "$log" ;;
    controls) grep -q 'menu: CONTROLS | Back | Analog stick: off | Controller help >' "$log" ;;
    extras) grep -q 'menu: EXTRAS | Back | Mesh viewer | Text pacing: off' "$log" ;;
  esac
done
# A submenu can be exited without closing the whole overlay. Values in the
# Graphics submenu change using the same D-pad Right event as on a gamepad.
for step in back fps; do
  case "$step" in
    back) nav='down right esc' ;;
    fps) nav='down right down down down right' ;;
  esac
  FA18_MENU_NAV="$nav" FA18_MENU_TRACE=1 FA18_MENU_SHOT="$tmp/$step.ppm" \
    "$bin" --native on --fast --frames 5 \
    --script "$repo/tests/data/debug_menu_pages.txt" "$adf" >"$tmp/$step.log" 2>&1
  test -s "$tmp/$step.ppm"
  if [ "$step" = back ]; then
    grep -q 'menu: DEBUG MENU | Resume | Graphics >' "$tmp/back.log"
  else
    grep -q 'menu: GRAPHICS | Back | 3D renderer: .* | Widescreen: off | Render FPS: 120 (OpenGL)' "$tmp/fps.log"
  fi
done
echo 'PASS: debug-menu pages and navigation labels render through the host'
