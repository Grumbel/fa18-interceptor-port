#!/usr/bin/env bash
# Compare the original two-writer reticle latch against bitmap-stencil gating.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_reticle_capture.sh ORIGINAL.adf [fa18-executable]' >&2
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
  FA18_HUD_RETICLE_CAPTURE=0 "$bin" "${common[@]}" "${args[@]}" \
    --screenshot "$frame" "$tmp/$mode-old.ppm" "$adf" >"$tmp/$mode-old.log" 2>&1
  FA18_HUD_RETICLE_CAPTURE_DEBUG=1 "$bin" "${common[@]}" "${args[@]}" \
    --screenshot "$frame" "$tmp/$mode-native.ppm" "$adf" >"$tmp/$mode-native.log" 2>&1
  cmp "$tmp/$mode-old.ppm" "$tmp/$mode-native.ppm"
done

if ! grep -q '^HUD reticle capture use frame=4000 writer=1$' \
  "$tmp/flight-native.log"; then
  echo 'FAIL: reticle not captured from the original two drawing branches' >&2
  grep 'HUD reticle' "$tmp/flight-native.log" | tail -15 >&2 || true
  exit 1
fi

echo 'PASS: original reticle writer capture matches bitmap fallback in flight/Pause'
