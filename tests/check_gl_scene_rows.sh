#!/usr/bin/env bash
# Check shared bank/scene row selection and separate cockpit/side copper timebases.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_scene_rows.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
# The historical pixel-centre oracle does not resolve all radar subpixels.
export FA18_COCKPIT_RADAR_NATIVE=0

for mode in bank flight pause maneuver; do
  case "$mode" in
    bank) limit=3545; script="$repo/tests/scenarios/free1.txt" ;;
    flight) limit=4010; script="$repo/tests/scenarios/free1.txt" ;;
    pause) limit=3590; script="$repo/tests/data/pause_direct_gl.txt" ;;
    maneuver) limit=4660; script="$repo/tests/data/cockpit_radar_maneuver.txt" ;;
  esac
  args=(--native off --interpret --det-timing --fast --gl 3 --widescreen
        --render-fps 120 --frames "$limit" --ram-hash 100 --script "$script")
  "$bin" "${args[@]}" "$adf" >"$tmp/$mode-cpu.log" 2>&1
  FA18_GL_DIRECT_COMPARE=1 "$bin" "${args[@]}" --gl-direct "$adf" \
    >"$tmp/$mode-gpu.log" 2>&1

  if ! awk '$1 == "gl:" && $2 == "direct" && $3 == "parity" &&
              $4 == "frame" { split($7, diff, "/"); seen = 1;
                             if (diff[1] > 2) bad = 1 }
              END { exit !(seen && !bad) }' "$tmp/$mode-gpu.log"; then
    echo "FAIL: $mode direct GPU / CPU scene-row or copper parity" >&2
    grep '^gl: direct parity frame' "$tmp/$mode-gpu.log" >&2 || true
    exit 1
  fi
  cmp <(grep '^\[ramhash\]' "$tmp/$mode-cpu.log") \
      <(grep '^\[ramhash\]' "$tmp/$mode-gpu.log")
done

echo 'PASS: shared scene-row selection, live cockpit copper, GPU/CPU and RAM parity'
