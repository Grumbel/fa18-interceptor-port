#!/usr/bin/env bash
# Native central radar: original moving-contact oracle, bounds, memory, GPU.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_radar_vector.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

for scenario in baseline pull; do
  if [ "$scenario" = baseline ]; then
    frame=3810; dot_x=150; dot_y=174
    script="$repo/tests/scenarios/free1.txt"
  else
    frame=4650; dot_x=166; dot_y=160
    script="$repo/tests/data/cockpit_radar_maneuver.txt"
  fi
  source=(--native off --interpret --det-timing --fast
          --frames "$((frame + 10))" --script "$script" --screenshot "$frame")
  "$bin" "${source[@]}" "$tmp/$scenario-source.ppm" "$adf" \
    >"$tmp/$scenario-source.log" 2>&1
  python3 "$repo/tests/check_gl_radar_vector.py" --original \
    "$tmp/$scenario-source.ppm" "$dot_x" "$dot_y"

  args=(--native off --interpret --det-timing --fast --gl 3 --widescreen
        --render-fps 120 --frames "$((frame + 10))" --script "$script"
        --ram-hash 200 --screenshot "$frame")
  FA18_COCKPIT_RADAR_NATIVE=0 \
    "$bin" "${args[@]}" "$tmp/$scenario-old.ppm" "$adf" \
    >"$tmp/$scenario-old.log" 2>&1
  FA18_RADAR_WRITER_DEBUG=1 FA18_RADAR_BLIT_DEBUG=1 \
    FA18_RADAR_STENCIL_DEBUG=1 \
    "$bin" "${args[@]}" "$tmp/$scenario-new.ppm" "$adf" \
    >"$tmp/$scenario-new.log" 2>&1
  python3 "$repo/tests/check_gl_radar_vector.py" --writer \
    "$tmp/$scenario-new.log" "$frame" "$dot_x" "$dot_y"
  python3 "$repo/tests/check_gl_radar_vector.py" --stencil \
    "$tmp/$scenario-new.log" "$frame"
  python3 "$repo/tests/check_gl_radar_vector.py" \
    "$tmp/$scenario-old.ppm" "$tmp/$scenario-new.ppm"
  cmp <(grep '^\[ramhash\]' "$tmp/$scenario-old.log") \
      <(grep '^\[ramhash\]' "$tmp/$scenario-new.log")

  # Diagnostic A/B gate: the old source-pixel stencil recogniser should
  # select the same radar in settled flight, now without being consulted by
  # the writer-derived production path.
  FA18_RADAR_BITMAP_REFERENCE=1 "$bin" "${args[@]}" \
    "$tmp/$scenario-reference.ppm" "$adf" \
    >"$tmp/$scenario-reference.log" 2>&1
  cmp "$tmp/$scenario-new.ppm" "$tmp/$scenario-reference.ppm"
done

for mode in legacy vector; do
  if [ "$mode" = legacy ]; then
    export FA18_COCKPIT_RADAR_NATIVE=0
  else
    unset FA18_COCKPIT_RADAR_NATIVE
  fi
  FA18_GL_DIRECT_COMPARE=1 FA18_GL_DIRECT_DEBUG=1 \
    FA18_GL_DIRECT_SUBPIXEL_PARITY=1 \
    "$bin" --native off --interpret --det-timing --fast --gl 3 --gl-direct \
    --widescreen --render-fps 120 --frames 3830 \
    --script "$repo/tests/scenarios/free1.txt" "$adf" \
    >"$tmp/direct-$mode.log" 2>&1
  if ! grep -q 'gl: direct GPU presentation 1' "$tmp/direct-$mode.log"; then
    echo "FAIL: no direct GPU presentation ($mode)" >&2
    exit 1
  fi
done
# This test compares the mismatch *delta* from replacing the original radar
# glyphs; the shared, known 3x HUD/scene edge discrepancies are unrelated.
# The viewport's 720 physical rows are sampled against the corresponding
# 600-row CPU *subpixels* (not a constant centre subpixel).
parity_at_3810() {
  awk '$1 == "gl:" && $2 == "direct" && $3 == "parity" &&
       $4 == "frame" && $5 == 3810 {
         split($7, count, "/"); print count[1]
       }' "$1"
}
legacy=$(parity_at_3810 "$tmp/direct-legacy.log")
vector=$(parity_at_3810 "$tmp/direct-vector.log")
if [ -z "$legacy" ] || [ -z "$vector" ] ||
   [ "$legacy" -gt 8 ] || [ "$legacy" != "$vector" ]; then
  echo "FAIL: radar added direct GPU/CPU parity differences: $legacy -> $vector" >&2
  exit 1
fi
echo "PASS: direct GPU radar adds zero parity differences (baseline $legacy)"
echo 'PASS: native radar source, bounds, RAM and direct GPU parity'
