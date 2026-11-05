#!/usr/bin/env bash
# Verify five original HUD-outline line commands per Amiga screen bank and
# compare the writer/copper visibility path to the legacy source-pixel gate.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: check_gl_hud_frame_capture.sh ORIGINAL.adf [fa18-executable]' >&2
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
        --widescreen --render-fps 120 --frames "$limit" --ram-hash 100
        --script "$script" --screenshot "$frame")
  FA18_HUD_FRAME_BITMAP_REFERENCE=1 \
    "$bin" "${args[@]}" "$tmp/$mode-reference.ppm" "$adf" \
    >"$tmp/$mode-reference.log" 2>&1
  FA18_HUD_FRAME_CAPTURE_DEBUG=1 \
    "$bin" "${args[@]}" "$tmp/$mode-writer.ppm" "$adf" \
    >"$tmp/$mode-writer.log" 2>&1
  cmp "$tmp/$mode-reference.ppm" "$tmp/$mode-writer.ppm"
  cmp <(grep '^\[ramhash\]' "$tmp/$mode-reference.log") \
      <(grep '^\[ramhash\]' "$tmp/$mode-writer.log")
  echo "PASS: $mode original outline writer and pixel gate produce identical HUD"
done

python3 "$repo/tests/check_gl_hud_frame_capture.py" "$tmp/flight-writer.log"
echo 'PASS: per-bank HUD outline capture, colour, backdrop, and RAM parity'
