#!/usr/bin/env bash
# The original 3x5 HUD writer and compass point writer provide every original
# ink location. Compare native glyph-footprint clearing to the bitmap stencil
# across screen-bank changes, flight, Pause and cockpit updates.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_hud_text_erase.sh ORIGINAL.adf [fa18-executable]' >&2
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
  args=(--native off --interpret --det-timing --fast --gl 3 --widescreen
        --render-fps 120 --frames "$limit" --ram-hash 100
        --script "$script" --screenshot "$frame")
  FA18_HUD_TEXT_ERASE_BITMAP_REFERENCE=1 \
    "$bin" "${args[@]}" "$tmp/$mode-reference.ppm" "$adf" \
    >"$tmp/$mode-reference.log" 2>&1
  FA18_HUD_CAPTURE_DEBUG=1 FA18_HUD_MARK_CAPTURE_DEBUG=1 \
    FA18_HUD_TEXT_ERASE_DEBUG=1 \
    "$bin" "${args[@]}" "$tmp/$mode-writer.ppm" "$adf" \
    >"$tmp/$mode-writer.log" 2>&1
  cmp "$tmp/$mode-reference.ppm" "$tmp/$mode-writer.ppm"
  cmp <(grep '^\[ramhash\]' "$tmp/$mode-reference.log") \
      <(grep '^\[ramhash\]' "$tmp/$mode-writer.log")
  echo "PASS: $mode native HUD text erase matches bitmap pixels and RAM"
done

grep -Eq '^HUD capture use frame=4000 digits=[1-9][0-9]* letters=[1-9][0-9]*' \
  "$tmp/flight-writer.log"
grep -Eq '^HUD heading capture use frame=4000 writer=1 phase=1(3[89]|4[0-8])$' \
  "$tmp/flight-writer.log"
grep -Eq '^HUD text erase capture frame=4000 digits=[1-9][0-9]* letters=[1-9][0-9]* heading=1$' \
  "$tmp/flight-writer.log"

# Screenshot capture can switch out of direct GL presentation. Observe the
# *visible* GPU compositor's CPU parity in addition to screenshot hashes.
for mode in flight pause; do
  case "$mode" in
    flight) limit=4010; script="$repo/tests/scenarios/free1.txt" ;;
    pause) limit=3590; script="$repo/tests/data/pause_direct_gl.txt" ;;
  esac
  for variant in writer reference; do
    flags=(FA18_GL_DIRECT_COMPARE=1 FA18_COCKPIT_RADAR_NATIVE=0)
    if [ "$variant" = reference ]; then
      flags+=(FA18_HUD_TEXT_ERASE_BITMAP_REFERENCE=1)
    fi
    env "${flags[@]}" "$bin" --native off --interpret --det-timing \
      --fast --gl 3 --gl-direct --widescreen --render-fps 120 \
      --frames "$limit" --ram-hash 100 --script "$script" "$adf" \
      >"$tmp/$mode-$variant-direct.log" 2>&1
    # The existing 3x raster edge may differ by one/two sample centres.
    if ! awk '$1 == "gl:" && $2 == "direct" && $3 == "parity" &&
              $4 == "frame" { split($7, diff, "/"); seen = 1;
                             if (diff[1] > 2) bad = 1 }
              END { exit !(seen && !bad) }' \
        "$tmp/$mode-$variant-direct.log"; then
      echo "FAIL: $mode $variant native HUD GPU/CPU parity diverged" >&2
      grep '^gl: direct parity frame ' "$tmp/$mode-$variant-direct.log" >&2
      exit 1
    fi
  done
  cmp <(grep '^\[ramhash\]' "$tmp/$mode-writer-direct.log") \
      <(grep '^\[ramhash\]' "$tmp/$mode-reference-direct.log")
done
echo 'PASS: completed HUD glyph/compass writer erase, GPU/CPU parity and game RAM'
