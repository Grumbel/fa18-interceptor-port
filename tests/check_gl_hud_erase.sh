#!/usr/bin/env bash
# Compare complete writer-footprint erasure with the original RGB-matched
# bitmap stencil, in CPU-composed screenshots and visible GPU parity traces.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_hud_erase.sh ORIGINAL.adf [fa18-executable]' >&2
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
  FA18_HUD_ERASE_BITMAP_REFERENCE=1 \
    "$bin" "${args[@]}" "$tmp/$mode-reference.ppm" "$adf" \
    >"$tmp/$mode-reference.log" 2>&1
  FA18_HUD_FRAME_CAPTURE_DEBUG=1 FA18_HUD_RETICLE_CAPTURE_DEBUG=1 \
    "$bin" "${args[@]}" "$tmp/$mode-writer.ppm" "$adf" \
    >"$tmp/$mode-writer.log" 2>&1
  cmp "$tmp/$mode-reference.ppm" "$tmp/$mode-writer.ppm"
  cmp <(grep '^\[ramhash\]' "$tmp/$mode-reference.log") \
      <(grep '^\[ramhash\]' "$tmp/$mode-writer.log")
  echo "PASS: $mode capture-based HUD erasure matches original pixels and RAM"
done

# --gl-direct screenshots can use the SDL fallback; additionally run the
# real direct-compositor GPU/CPU oracle, once in flight and once in Pause.
# The existing radar hairline difference is unrelated to this HUD migration.
for mode in flight pause; do
  case "$mode" in
    flight) limit=4010; script="$repo/tests/scenarios/free1.txt" ;;
    pause) limit=3590; script="$repo/tests/data/pause_direct_gl.txt" ;;
  esac
  for variant in writer reference; do
    flags=(FA18_GL_DIRECT_COMPARE=1 FA18_COCKPIT_RADAR_NATIVE=0)
    if [ "$variant" = reference ]; then
      flags+=(FA18_HUD_ERASE_BITMAP_REFERENCE=1)
    fi
    env "${flags[@]}" "$bin" --native off --interpret --det-timing \
      --fast --gl 3 --gl-direct --widescreen --render-fps 120 \
      --frames "$limit" --ram-hash 100 --script "$script" "$adf" \
      >"$tmp/$mode-$variant-direct.log" 2>&1
    # The 3x framebuffer already has a known one/two-centre raster edge
    # discrepancy. Larger divergences indicate missing HUD overlay pixels.
    if ! awk '$1 == "gl:" && $2 == "direct" && $3 == "parity" &&
              $4 == "frame" { split($7, diff, "/"); seen = 1;
                             if (diff[1] > 2) bad = 1 }
              END { exit !(seen && !bad) }' \
        "$tmp/$mode-$variant-direct.log"; then
      echo "FAIL: $mode $variant GPU/CPU compositor diverged" >&2
      grep '^gl: direct parity frame ' "$tmp/$mode-$variant-direct.log" >&2
      exit 1
    fi
  done
  cmp <(grep '^\[ramhash\]' "$tmp/$mode-writer-direct.log") \
      <(grep '^\[ramhash\]' "$tmp/$mode-reference-direct.log")
  echo "PASS: $mode capture-based erase is GPU/CPU equivalent"
done

grep -q '^HUD frame capture use frame=4000 writer=1 visible=1 rails=1$' \
  "$tmp/flight-writer.log"
grep -q '^HUD reticle capture use frame=4000 writer=1$' \
  "$tmp/flight-writer.log"
echo 'PASS: original HUD writers selected; bitmap fallback remains available'
