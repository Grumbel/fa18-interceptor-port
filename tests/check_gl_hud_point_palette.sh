#!/usr/bin/env bash
# Pen-13 heading ticks and pen-10 reticle are now coloured from the copper
# palette after original writer capture. Compare to the previous bitmap-ink
# path across screen-bank flips, a live flight, Pause and changing controls.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: check_gl_hud_point_palette.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

for mode in bank flight pause dynamic; do
  case "$mode" in
    bank) frame=3538; limit=3545; script="$repo/tests/scenarios/free1.txt" ;;
    flight) frame=4000; limit=4010; script="$repo/tests/scenarios/free1.txt" ;;
    pause) frame=3570; limit=3580; script="$repo/tests/data/pause_gl.txt" ;;
    dynamic) frame=3960; limit=3970; script="$repo/tests/data/cockpit_dynamic.txt" ;;
  esac
  args=(--native off --interpret --det-timing --fast --gl 3
        --widescreen --render-fps 120 --frames "$limit"
        --script "$script" --ram-hash 100)
  FA18_HUD_POINT_PALETTE_REFERENCE=1 \
    "$bin" "${args[@]}" --screenshot "$frame" "$tmp/$mode-old.ppm" "$adf" \
    >"$tmp/$mode-old.log" 2>&1
  FA18_HUD_RETICLE_CAPTURE_DEBUG=1 FA18_HUD_MARK_CAPTURE_DEBUG=1 \
    "$bin" "${args[@]}" --screenshot "$frame" "$tmp/$mode-new.ppm" "$adf" \
    >"$tmp/$mode-new.log" 2>&1
  cmp "$tmp/$mode-old.ppm" "$tmp/$mode-new.ppm"
  cmp <(grep '^\[ramhash\]' "$tmp/$mode-old.log") \
      <(grep '^\[ramhash\]' "$tmp/$mode-new.log")
done

grep -q '^HUD reticle capture use frame=4000 writer=1$' "$tmp/flight-new.log"
grep -Eq '^HUD heading capture use frame=4000 writer=1 phase=1(3[89]|4[0-8])$' \
  "$tmp/flight-new.log"
echo 'PASS: heading/reticle copper ink matches original pixel sampling and RAM'
