#!/usr/bin/env bash
# Asset-backed cockpit parity against the previous bitplane-backed path.
# All artwork is decoded from the user's ADF; neither the test nor the
# executable contains a copy of the original proprietary images.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_cockpit.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

for mode in flight pause; do
  if [ "$mode" = flight ]; then
    frame=4000
    args=(--frames 4010 --script "$repo/tests/scenarios/free1.txt")
  else
    frame=3570
    args=(--frames 3580 --script "$repo/tests/data/pause_gl.txt")
  fi
  common=(--native off --interpret --det-timing --fast --gl 3 --widescreen
          --render-fps 120 "${args[@]}")
  FA18_COCKPIT_NATIVE=0 "$bin" "${common[@]}" \
    --screenshot "$frame" "$tmp/$mode-original.ppm" "$adf" \
    >"$tmp/$mode-original.log" 2>&1
  FA18_COCKPIT_DEBUG=1 "$bin" "${common[@]}" \
    --screenshot "$frame" "$tmp/$mode-native.ppm" "$adf" \
    >"$tmp/$mode-native.log" 2>&1
  cmp "$tmp/$mode-original.ppm" "$tmp/$mode-native.ppm"
  if ! grep -q 'cockpit: 21056 native static artwork pixels available' \
      "$tmp/$mode-native.log"; then
    echo "FAIL: $mode did not load original cockpit artwork" >&2
    exit 1
  fi
  if ! grep -Eq 'cockpit: CPU frame [0-9]+ native static=[1-9][0-9]{4,}' \
      "$tmp/$mode-native.log"; then
    echo "FAIL: $mode did not actually compose a native cockpit layer" >&2
    exit 1
  fi
done

echo 'PASS: original ILBM cockpit composition is pixel-exact during flight and Pause'
