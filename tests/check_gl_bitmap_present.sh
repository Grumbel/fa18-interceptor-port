#!/usr/bin/env bash
# The visible GL window must not hand the swapchain to SDL when a 3D capture
# is unavailable or the user chooses original 320x200 graphics in the menu.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_bitmap_present.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.log "$tmp"/*.ppm; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

# The first frames are still the original 2D display. Compare every source
# pixel at its physical sample centre, including the corrected 4:3 aspect.
for mode in centred wide; do
  args=()
  if [ "$mode" = wide ]; then args=(--widescreen); fi
  FA18_GL_BITMAP_DEBUG=1 FA18_GL_BITMAP_COMPARE=1 \
    "$bin" --native off --interpret --gl-direct "${args[@]}" --frames 12 "$adf" \
    >"$tmp/$mode.log" 2>&1
  test "$(grep -c 'gl: bitmap parity frame .* different 0/' "$tmp/$mode.log")" -eq 12
  grep -q 'gl: native bitmap presentation 1 ' "$tmp/$mode.log"
  if grep -Eq 'gl: bitmap parity frame .* different [1-9][0-9]*/' "$tmp/$mode.log"; then
    echo "FAIL: $mode original 2D GPU/SDL pixel-centre parity" >&2
    exit 1
  fi
done

# With --gl-direct, switching the scene renderer to Original from the menu
# must continue on the GPU window; the GL context is still owned by the host.
FA18_MENU_NAV='down right down right right' \
  FA18_MENU_TRACE=1 FA18_MENU_SHOT="$tmp/menu.ppm" \
  FA18_GL_BITMAP_DEBUG=1 FA18_GL_BITMAP_COMPARE=1 \
  "$bin" --native off --interpret --gl-direct --frames 6 \
  --script "$repo/tests/data/debug_menu_pages.txt" "$adf" \
  >"$tmp/menu.log" 2>&1
test -s "$tmp/menu.ppm"
grep -q 'menu: GRAPHICS | Back | 3D renderer: original (320x200)' "$tmp/menu.log"
grep -q 'gl: native bitmap presentation 3 ' "$tmp/menu.log"
if grep -Eq 'gl: bitmap parity frame .* different [1-9][0-9]*/' "$tmp/menu.log"; then
  echo 'FAIL: GPU bitmap output after changing the scene renderer' >&2
  exit 1
fi

# Stats/FPS are a separate transparent 960x720 GPU layer at the window edge.
FA18_GL_BITMAP_DEBUG=1 \
  "$bin" --native off --interpret --gl-direct --stats --fps --frames 4 "$adf" \
  >"$tmp/stats.log" 2>&1
grep -q 'gl: native bitmap presentation 1 .* hud 1' "$tmp/stats.log"
if grep -Eq 'gl: native bitmap presentation .* error 0x[1-9a-f]' "$tmp/stats.log"; then
  echo 'FAIL: bitmap presenter generated GL errors' >&2
  exit 1
fi

echo 'PASS: original/menu/software GPU bitmap presentation, pixel parity and HUD overlay'
