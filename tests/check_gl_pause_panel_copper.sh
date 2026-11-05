#!/usr/bin/env bash
# Pause title and retained HUD outline without reading the old title pixels
# or the backdrop colour. Compare against independent bitplane reference gates.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: check_gl_pause_panel_copper.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

for frame in 3550 3553 3554 3570 3600; do
  args=(--native off --interpret --det-timing --fast --gl 3
        --widescreen --render-fps 120 --frames "$((frame + 10))"
        --ram-hash 100 --script "$repo/tests/data/pause_gl.txt" --screenshot "$frame")
  FA18_PAUSE_TITLE_BITMAP_REFERENCE=1 FA18_PAUSE_BACKDROP_REFERENCE=1 \
    FA18_HUD_FRAME_BITMAP_REFERENCE=1 \
    "$bin" "${args[@]}" "$tmp/$frame-reference.ppm" "$adf" \
    >"$tmp/$frame-reference.log" 2>&1
  FA18_PAUSE_TITLE_CAPTURE_DEBUG=1 FA18_HUD_FRAME_CAPTURE_DEBUG=1 \
    "$bin" "${args[@]}" "$tmp/$frame-copper.ppm" "$adf" \
    >"$tmp/$frame-copper.log" 2>&1
  cmp "$tmp/$frame-reference.ppm" "$tmp/$frame-copper.ppm"
  cmp <(grep '^\[ramhash\]' "$tmp/$frame-reference.log") \
      <(grep '^\[ramhash\]' "$tmp/$frame-copper.log")
  echo "PASS: Pause frame $frame native title, backdrop and retained outline"
done

grep -q '^Pause title capture use frame=3550 writer=1 painted=0 bitmap=0 visible=0$' \
  "$tmp/3550-copper.log"
grep -q '^Pause title capture use frame=3554 writer=1 painted=64 bitmap=64 visible=1$' \
  "$tmp/3554-copper.log"
grep -q '^HUD frame capture use frame=3570 writer=1 visible=1 rails=1$' \
  "$tmp/3570-copper.log"
echo 'PASS: no original Pause title/outline pixel gate, correct first draw and persistent frame'
