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
  FA18_COCKPIT_DEBUG=1 FA18_COCKPIT_FUEL_DEBUG=1 \
    FA18_COCKPIT_TEXT_DEBUG=1 "$bin" "${common[@]}" \
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
  if [ "$mode" = flight ] &&
     ! grep -q 'cockpit: native fuel LCD [0-9]\{5\}' "$tmp/$mode-native.log"; then
    echo 'FAIL: native fuel digits were not drawn from the player record' >&2
    exit 1
  fi
  if [ "$mode" = flight ] &&
     ! grep -q 'cockpit: native altitude/airspeed glyphs' "$tmp/$mode-native.log"; then
    echo 'FAIL: native altitude/airspeed glyphs were not composed' >&2
    exit 1
  fi
  if [ "$mode" = flight ]; then
    buffers=$(sed -n 's/^cockpit: fuel latch frame .* buffer=\([0-9a-f]*\) value=.*/\1/p' \
                "$tmp/$mode-native.log" | sort -u | wc -l)
    if [ "$buffers" -lt 2 ]; then
      echo 'FAIL: native fuel LCD did not capture both double buffers' >&2
      exit 1
    fi
    if ! grep -Eq 'cockpit: fuel latch frame .* value=24998' "$tmp/$mode-native.log"; then
      echo 'FAIL: native fuel LCD did not latch a changed fuel value' >&2
      exit 1
    fi
  fi
done

# The recompiler executes multiple 68000 instructions per dispatch. Its
# generated chunk must break at $4744e or the native redraw PC hook never
# fires, silently leaving the original bitplane digits as a fallback.
FA18_COCKPIT_FUEL_DEBUG=1 FA18_COCKPIT_TEXT_DEBUG=1 \
  "$bin" --native off --det-timing --fast --gl 1 \
  --frames 3550 --script "$repo/tests/scenarios/free1.txt" "$adf" \
  >"$tmp/recompiled-fuel.log" 2>&1
if [ "$(grep -c 'cockpit: observed fuel display buffer' "$tmp/recompiled-fuel.log")" -lt 2 ] ||
   ! grep -Eq 'cockpit: fuel latch frame .* value=24999' "$tmp/recompiled-fuel.log"; then
  echo 'FAIL: recompiled fuel LCD bypassed the native redraw hook' >&2
  exit 1
fi
if ! grep -q 'cockpit: right glyph frame=' "$tmp/recompiled-fuel.log"; then
  echo 'FAIL: recompiled right-hand digits bypassed the native font-writer hook' >&2
  exit 1
fi

echo 'PASS: native ILBM cockpit, fuel LCD and right-hand glyphs are pixel-exact during flight and Pause'
