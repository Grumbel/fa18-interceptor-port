#!/usr/bin/env bash
# Original Pause banner visibility from the 5x7 font writer, with the old
# displayed-pixel count retained as an independent regression oracle.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: check_gl_pause_title_capture.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

for frame in 3550 3552 3553 3554 3555 3557 3570; do
  args=(--native off --interpret --det-timing --fast --gl 3
        --widescreen --render-fps 120 --frames "$((frame + 10))"
        --ram-hash 100 --script "$repo/tests/data/pause_gl.txt" --screenshot "$frame")
  FA18_PAUSE_TITLE_BITMAP_REFERENCE=1 \
    "$bin" "${args[@]}" "$tmp/$frame-reference.ppm" "$adf" \
    >"$tmp/$frame-reference.log" 2>&1
  FA18_PAUSE_TITLE_CAPTURE_DEBUG=1 \
    "$bin" "${args[@]}" "$tmp/$frame-capture.ppm" "$adf" \
    >"$tmp/$frame-capture.log" 2>&1
  cmp "$tmp/$frame-reference.ppm" "$tmp/$frame-capture.ppm"
  cmp <(grep '^\[ramhash\]' "$tmp/$frame-reference.log") \
      <(grep '^\[ramhash\]' "$tmp/$frame-capture.log")
  echo "PASS: Pause title frame $frame matches source-pixel visibility exactly"
done

python3 "$repo/tests/check_gl_pause_title_capture.py" "$tmp/3570-capture.log"
echo 'PASS: partial and complete Pause title captures match original ink and timing'
