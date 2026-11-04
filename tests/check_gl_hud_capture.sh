#!/usr/bin/env bash
# Verify HUD source-font interception against the previous exact-stencil
# recogniser, including the per-buffer redraws and live copper colours.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_hud_capture.sh ORIGINAL.adf [fa18-executable]' >&2
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
  FA18_HUD_FONT_CAPTURE=0 "$bin" "${common[@]}" "${args[@]}" \
    --screenshot "$frame" "$tmp/$mode-old.ppm" "$adf" >"$tmp/$mode-old.log" 2>&1
  FA18_HUD_CAPTURE_DEBUG=1 "$bin" "${common[@]}" "${args[@]}" \
    --screenshot "$frame" "$tmp/$mode-native.ppm" "$adf" >"$tmp/$mode-native.log" 2>&1
  cmp "$tmp/$mode-old.ppm" "$tmp/$mode-native.ppm"
done

if ! grep -Eq '^HUD capture use frame=4000 digits=[1-9][0-9]* letters=[1-9][0-9]*' \
  "$tmp/flight-native.log"; then
  echo 'FAIL: HUD glyphs not captured from the original per-buffer font writer' >&2
  grep 'HUD capture' "$tmp/flight-native.log" | tail -15 >&2 || true
  exit 1
fi

echo 'PASS: original-font HUD capture matches CPU/GL compositor stencil fallback across flight/Pause'
