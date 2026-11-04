#!/usr/bin/env bash
# Verify native HUD crown placement/AA against the original source strokes.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_frame.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

common=(--native off --interpret --det-timing --fast --gl 3
        --widescreen --render-fps 120)
for mode in flight pause; do
  if [ "$mode" = flight ]; then
    frame=4000
    args=(--frames 4010 --script "$repo/tests/scenarios/free1.txt")
  else
    frame=3570
    args=(--frames 3580 --script "$repo/tests/data/pause_gl.txt")
  fi
  FA18_HUD_FRAME_NATIVE=0 "$bin" "${common[@]}" "${args[@]}" \
    --screenshot "$frame" "$tmp/$mode-old.ppm" "$adf" >"$tmp/$mode-old.log" 2>&1
  "$bin" "${common[@]}" "${args[@]}" \
    --screenshot "$frame" "$tmp/$mode-native.ppm" "$adf" >"$tmp/$mode-native.log" 2>&1
  python3 "$repo/tests/check_gl_frame.py" \
    "$tmp/$mode-native.ppm" "$tmp/$mode-old.ppm"
done

echo 'PASS: native flight and faded-Pause HUD crown bounded and antialiased'
