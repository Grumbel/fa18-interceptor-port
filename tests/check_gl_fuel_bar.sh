#!/usr/bin/env bash
# Original-game quarter-tank oracle + bounded high-res native fuel bar checks.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_fuel_bar.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

for level in full three-quarters half quarter; do
  case "$level" in
    full)          script="$repo/tests/scenarios/free1.txt"; rows=22 ;;
    three-quarters) script="$repo/tests/data/cockpit_fuel_three_quarters.txt"; rows=17 ;;
    half)          script="$repo/tests/data/cockpit_fuel_half.txt"; rows=11 ;;
    quarter)       script="$repo/tests/data/cockpit_fuel_quarter.txt"; rows=5 ;;
  esac
  "$bin" --native off --interpret --det-timing --fast --frames 3820 \
    --script "$script" --screenshot 3810 "$tmp/$level-source.ppm" "$adf" \
    >"$tmp/$level-source.log" 2>&1
  python3 "$repo/tests/check_gl_fuel_bar.py" --original \
    "$tmp/$level-source.ppm" "$rows"
done

for level in half three_quarters; do
  script="$repo/tests/data/cockpit_fuel_$level.txt"
  args=(--native off --interpret --det-timing --fast --gl 3 --widescreen
        --render-fps 120 --frames 3820 --script "$script" --ram-hash 100
        --screenshot 3810)
  FA18_COCKPIT_FUEL_BAR_NATIVE=0 \
    "$bin" "${args[@]}" "$tmp/$level-old.ppm" "$adf" \
    >"$tmp/$level-old.log" 2>&1
  "$bin" "${args[@]}" "$tmp/$level-new.ppm" "$adf" \
    >"$tmp/$level-new.log" 2>&1
  python3 "$repo/tests/check_gl_fuel_bar.py" \
    "$tmp/$level-old.ppm" "$tmp/$level-new.ppm"
  cmp <(grep '^\[ramhash\]' "$tmp/$level-old.log") \
      <(grep '^\[ramhash\]' "$tmp/$level-new.log")
done

# The direct compositor uses a uniform instead of a new gauge texture. Its
# native edge coverage must match the CPU output at the same sample centres.
FA18_GL_DIRECT_COMPARE=1 FA18_GL_DIRECT_DEBUG=1 \
  "$bin" --native off --interpret --det-timing --fast --gl 3 --gl-direct \
  --widescreen --render-fps 120 --frames 3830 \
  --script "$repo/tests/data/cockpit_fuel_half.txt" "$adf" \
  >"$tmp/direct.log" 2>&1
if ! grep -q 'gl: direct GPU presentation 1' "$tmp/direct.log"; then
  echo 'FAIL: native gauge did not reach direct GPU presentation' >&2
  exit 1
fi
# Two known scene-edge pixels around the central 3D view differ in the
# software GL 3x reference at VBL 3810 (outside the cockpit). The native
# bar must not add any mismatch: compare the stable half-tank frame and
# reject differences in the centred gauge rectangle (side = 54 pixels).
if ! awk '
  $1 == "gl:" && $2 == "direct" && $3 == "diff" && $4 == "at" {
    split($5, pos, ",");
    if (pos[1] >= 165 && pos[1] < 169 && pos[2] >= 158 && pos[2] < 181)
      gauge_bad = 1;
  }
  $1 == "gl:" && $2 == "direct" && $3 == "parity" &&
  $4 == "frame" && $5 == 3810 {
    split($7, count, "/");
    seen = 1;
    if (count[1] > 2 || gauge_bad) bad = 1;
  }
  END { exit !(seen && !bad) }
' "$tmp/direct.log"; then
  grep 'gl: direct parity frame' "$tmp/direct.log" >&2
  echo 'FAIL: GPU fuel bar differs from the CPU composer' >&2
  exit 1
fi
echo 'PASS: fuel gauge levels, high-res region, RAM and direct GPU parity'
