#!/usr/bin/env bash
# Verify native Pause text colour and visibility against the original ADF.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: check_gl_pause_ink.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

common=(--native off --interpret --det-timing --fast
        --script "$repo/tests/data/pause_gl.txt" --ram-hash 100)
"$bin" "${common[@]}" --frames 3580 \
  --screenshot 3570 "$tmp/original.ppm" "$adf" >"$tmp/original.log" 2>&1
for point in early title; do
  if [ "$point" = early ]; then frame=3550; else frame=3570; fi
  "$bin" "${common[@]}" --frames $((frame + 10)) \
    --gl 3 --widescreen --render-fps 120 \
    --screenshot "$frame" "$tmp/$point.ppm" "$adf" >"$tmp/$point.log" 2>&1
done
python3 "$repo/tests/check_gl_pause_ink.py" \
  "$tmp/original.ppm" "$tmp/early.ppm" "$tmp/title.ppm"
cmp <(grep '^\[ramhash\]' "$tmp/original.log" | head -n 35) \
    <(grep '^\[ramhash\]' "$tmp/title.log" | head -n 35)
