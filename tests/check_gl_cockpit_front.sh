#!/usr/bin/env bash
# Test independently reconstructed ADF front-nose artwork in both presenters.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_cockpit_front.sh ORIGINAL.adf [fa18-executable]' >&2
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
  FA18_COCKPIT_FRONT_BITMAP_REFERENCE=1 \
    "$bin" "${args[@]}" "$tmp/$mode-reference.ppm" "$adf" \
    >"$tmp/$mode-reference.log" 2>&1
  FA18_COCKPIT_FRONT_DEBUG=1 \
    "$bin" "${args[@]}" "$tmp/$mode-native.ppm" "$adf" \
    >"$tmp/$mode-native.log" 2>&1
  python3 "$repo/tests/check_gl_cockpit_front.py" "$adf" \
    "$tmp/$mode-reference.ppm" "$tmp/$mode-native.ppm"
  cmp <(grep '^\[ramhash\]' "$tmp/$mode-reference.log") \
      <(grep '^\[ramhash\]' "$tmp/$mode-native.log")
done
grep -Eq '^cockpit front CPU frame=4000 pixels=[1-9][0-9]*$' \
  "$tmp/flight-native.log"

for mode in flight pause; do
  case "$mode" in
    flight) limit=4010; script="$repo/tests/scenarios/free1.txt" ;;
    pause) limit=3590; script="$repo/tests/data/pause_direct_gl.txt" ;;
  esac
  for variant in native reference; do
    flags=(FA18_GL_DIRECT_COMPARE=1 FA18_COCKPIT_RADAR_NATIVE=0
           FA18_COCKPIT_FRONT_DEBUG=1)
    if [ "$variant" = reference ]; then
      flags+=(FA18_COCKPIT_FRONT_BITMAP_REFERENCE=1)
    fi
    env "${flags[@]}" "$bin" --native off --interpret --det-timing \
      --fast --gl 3 --gl-direct --widescreen --render-fps 120 \
      --frames "$limit" --ram-hash 100 --script "$script" "$adf" \
      >"$tmp/$mode-$variant-direct.log" 2>&1
    if ! awk '$1 == "gl:" && $2 == "direct" && $3 == "parity" &&
              $4 == "frame" { split($7, diff, "/"); seen = 1;
                             if (diff[1] > 2) bad = 1 }
              END { exit !(seen && !bad) }' \
        "$tmp/$mode-$variant-direct.log"; then
      echo "FAIL: $mode $variant GPU/CPU parity diverged" >&2
      grep '^gl: direct parity frame ' "$tmp/$mode-$variant-direct.log" >&2
      exit 1
    fi
  done
  cmp <(grep '^\[ramhash\]' "$tmp/$mode-native-direct.log") \
      <(grep '^\[ramhash\]' "$tmp/$mode-reference-direct.log")
done
echo 'PASS: native ADF cockpit nose, bounded pixels, GPU/CPU parity, RAM'
