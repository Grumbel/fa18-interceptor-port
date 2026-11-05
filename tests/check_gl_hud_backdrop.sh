#!/usr/bin/env bash
# Native HUD text/heading/reticle visibility from copper foreground AND
# backdrop pens, with the legacy source-background pixel as an A/B oracle.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: check_gl_hud_backdrop.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

for mode in bank flight pause dynamic maneuver; do
  case "$mode" in
    bank) frame=3538; limit=3545; script="$repo/tests/scenarios/free1.txt" ;;
    flight) frame=4000; limit=4010; script="$repo/tests/scenarios/free1.txt" ;;
    pause) frame=3570; limit=3580; script="$repo/tests/data/pause_gl.txt" ;;
    dynamic) frame=3960; limit=3970; script="$repo/tests/data/cockpit_dynamic.txt" ;;
    maneuver) frame=4650; limit=4660; script="$repo/tests/data/cockpit_radar_maneuver.txt" ;;
  esac
  args=(--native off --interpret --det-timing --fast --gl 3
        --widescreen --render-fps 120 --frames "$limit"
        --script "$script" --ram-hash 100 --screenshot "$frame")
  FA18_HUD_BACKDROP_REFERENCE=1 \
    "$bin" "${args[@]}" "$tmp/$mode-reference.ppm" "$adf" \
    >"$tmp/$mode-reference.log" 2>&1
  FA18_HUD_CAPTURE_DEBUG=1 FA18_HUD_MARK_CAPTURE_DEBUG=1 \
    FA18_HUD_RETICLE_CAPTURE_DEBUG=1 \
    "$bin" "${args[@]}" "$tmp/$mode-copper.ppm" "$adf" \
    >"$tmp/$mode-copper.log" 2>&1
  cmp "$tmp/$mode-reference.ppm" "$tmp/$mode-copper.ppm"
  cmp <(grep '^\[ramhash\]' "$tmp/$mode-reference.log") \
      <(grep '^\[ramhash\]' "$tmp/$mode-copper.log")
  echo "PASS: $mode copper backdrop and source-pixel backdrop are identical"
done

grep -Eq '^HUD capture use frame=4000 digits=[1-9]' "$tmp/flight-copper.log"
grep -q '^HUD reticle capture use frame=4000 writer=1$' "$tmp/flight-copper.log"
grep -q '^HUD heading capture use frame=4000 writer=1' "$tmp/flight-copper.log"
echo 'PASS: original HUD writer captures use only copper palette for ink/contrast'
