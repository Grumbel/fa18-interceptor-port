#!/usr/bin/env bash
# End-to-end experimental globe layer: actual ADF stencil, original 68000
# polygon capture, native CPU composition, selective bitplane masking and RAM.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_globe_native.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

# At VBL 4700 the original writer's projected horizon has a steady roll and
# both framebuffer banks have been observed. 120 Hz turns on the independent
# CPU cockpit compositor even for a headless --fast capture.
args=(--native off --interpret --det-timing --fast --gl 2 --render-fps 120
      --frames 4702 --ram-hash 100 --script "$repo/tests/data/cockpit_globe_roll.txt"
      --screenshot 4700)
"$bin" "${args[@]}" "$tmp/reference.ppm" "$adf" >"$tmp/reference.log" 2>&1
FA18_COCKPIT_GLOBE_NATIVE=1 FA18_COCKPIT_GLOBE_DEBUG=1 \
  FA18_COCKPIT_PANEL_DEBUG=1 \
  "$bin" "${args[@]}" "$tmp/native.ppm" "$adf" >"$tmp/native.log" 2>&1
FA18_COCKPIT_GLOBE_NATIVE=1 FA18_COCKPIT_GLOBE_DEBUG=1 \
  FA18_COCKPIT_PANEL_DEBUG=1 \
  "$bin" "${args[@]}" "$tmp/hidden.ppm" --hide-amiga-region globe "$adf" \
  >"$tmp/hidden.log" 2>&1

grep -q '^\[ramhash\]' "$tmp/reference.log"
for mode in native hidden; do
  cmp <(grep '^\[ramhash\]' "$tmp/reference.log") \
      <(grep '^\[ramhash\]' "$tmp/$mode.log")
  grep -Eq '^globe capture: frame=4693 bank=0a5a18 edge=' "$tmp/$mode.log"
  grep -Eq '^globe capture: frame=4697 bank=06a098 edge=' "$tmp/$mode.log"
  grep -Eq '^cockpit panel CPU frame=4400 pixels=[1-9][0-9]+' "$tmp/$mode.log"
done
grep -Fq 'graphics: original Amiga scanout region globe masked' "$tmp/hidden.log"

python3 -B "$repo/tests/check_gl_globe_native.py" "$adf" \
  "$tmp/reference.ppm" "$tmp/native.ppm" "$tmp/hidden.ppm"
echo 'PASS: per-bank native globe survives masking its original bitplanes, with matched pixels and RAM'
