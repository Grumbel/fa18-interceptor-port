#!/usr/bin/env bash
# ADF-backed region/bitmap test for seven native cockpit readouts.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: check_gl_readout_vector.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

for mode in flight pause dynamic; do
  case "$mode" in
    flight) frame=4000; limit=4010; script="$repo/tests/scenarios/free1.txt" ;;
    pause) frame=3570; limit=3580; script="$repo/tests/data/pause_gl.txt" ;;
    dynamic) frame=3960; limit=3970; script="$repo/tests/data/cockpit_dynamic.txt" ;;
  esac
  args=(--native off --interpret --det-timing --fast --gl 3
        --widescreen --render-fps 120 --frames "$limit"
        --script "$script" --ram-hash 100)
  FA18_COCKPIT_READOUTS_HIRES=0 \
    "$bin" "${args[@]}" --screenshot "$frame" "$tmp/$mode-old.ppm" "$adf" \
    >"$tmp/$mode-old.log" 2>&1
  FA18_COCKPIT_READOUT_VECTOR_DEBUG=1 \
    "$bin" "${args[@]}" --screenshot "$frame" "$tmp/$mode-new.ppm" "$adf" \
    >"$tmp/$mode-new.log" 2>&1
  check=("$tmp/$mode-old.ppm" "$tmp/$mode-new.ppm")
  if [ "$mode" = pause ]; then check+=(--allow-unchanged); fi
  python3 "$repo/tests/check_gl_readout_vector.py" "${check[@]}"
  cmp <(grep '^\[ramhash\]' "$tmp/$mode-old.log") \
      <(grep '^\[ramhash\]' "$tmp/$mode-new.log")
  if [ "$mode" != pause ] &&
     ! grep -q 'cockpit: [1-9][0-9]* complete native high-resolution readout digits' \
       "$tmp/$mode-new.log"; then
    echo "FAIL: $mode did not capture a complete readout glyph" >&2
    exit 1
  fi
done
echo 'PASS: native readout vectors stay confined to seven LCDs; RAM matches'
