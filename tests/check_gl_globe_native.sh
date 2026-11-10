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
  for bank in 06a098 0a5a18; do
    awk -v bank="$bank" '$1 == "globe" && $2 == "capture:" {
      split($3, frame, "="); split($4, actual, "=");
      if (frame[2] >= 4600 && actual[2] == bank) found = 1;
    } END { exit !found }' "$tmp/$mode.log"
  done
  grep -Eq '^cockpit panel CPU frame=4400 pixels=[1-9][0-9]+' "$tmp/$mode.log"
done
grep -Fq 'graphics: original Amiga scanout region globe masked' "$tmp/hidden.log"

python3 -B "$repo/tests/check_gl_globe_native.py" "$adf" \
  "$tmp/reference.ppm" "$tmp/native.ppm" "$tmp/hidden.ppm"

# Exercise the visible GPU compositor with the same rolled globe. The
# screenshot path above uses CPU composition even when OpenGL is active.
FA18_COCKPIT_GLOBE_NATIVE=1 FA18_COCKPIT_RADAR_NATIVE=0 \
  FA18_GL_DIRECT_COMPARE=1 FA18_GL_GLOBE_COMPARE=1 FA18_GL_DIRECT_DEBUG=1 \
  "$bin" --native off --interpret --det-timing --fast --gl 2 --gl-direct \
  --render-fps 120 --frames 4702 --script "$repo/tests/data/cockpit_globe_roll.txt" \
  "$adf" >"$tmp/direct.log" 2>&1
grep -q 'gl: direct GPU presentation 1' "$tmp/direct.log"
grep -Eq '^gl: globe shader frame=46[0-9][0-9] pixels=[1-9][0-9]*' "$tmp/direct.log"
if ! awk '$1 == "gl:" && $2 == "globe" && $3 == "parity" &&
          $4 == "frame" && $5 >= 4600 {
            split($7, diff, "/"); seen = 1;
            if (diff[1] > 2) bad = 1;
          } END { exit !(seen && !bad) }' "$tmp/direct.log"; then
  echo 'FAIL: rolled native globe direct GPU parity not established' >&2
  grep 'gl: globe parity' "$tmp/direct.log" >&2 || true
  exit 1
fi
FA18_COCKPIT_GLOBE_NATIVE=1 FA18_COCKPIT_RADAR_NATIVE=0 \
  FA18_GLOBE_FORCE_ATTITUDE=4300:4702:0:14400 \
  FA18_GL_DIRECT_COMPARE=1 FA18_GL_GLOBE_COMPARE=1 \
  "$bin" --native off --interpret --det-timing --fast --gl 2 --gl-direct \
  --render-fps 120 --frames 4702 --script "$repo/tests/data/cockpit_globe_level.txt" \
  "$adf" >"$tmp/inverted.log" 2>&1
grep -Eq '^gl: globe shader frame=46[0-9][0-9] pixels=358 inverted=358' "$tmp/inverted.log"
if ! awk '$1 == "gl:" && $2 == "globe" && $3 == "parity" &&
          $4 == "frame" && $5 >= 4600 {
            split($7, diff, "/"); seen = 1;
            if (diff[1] > 2) bad = 1;
          } END { exit !(seen && !bad) }' "$tmp/inverted.log"; then
  echo 'FAIL: inverted globe direct GPU parity not established' >&2
  grep 'gl: globe parity' "$tmp/inverted.log" >&2 || true
  exit 1
fi
for bank in 7200 21600; do
  FA18_COCKPIT_GLOBE_NATIVE=1 FA18_COCKPIT_RADAR_NATIVE=0 \
    FA18_GLOBE_FORCE_ATTITUDE="4300:4702:0:$bank" \
    FA18_GL_DIRECT_COMPARE=1 FA18_GL_GLOBE_COMPARE=1 \
    "$bin" --native off --interpret --det-timing --fast --gl 2 --gl-direct \
    --render-fps 120 --frames 4702 --script "$repo/tests/data/cockpit_globe_level.txt" \
    "$adf" >"$tmp/quarter.log" 2>&1
  grep -Eq '^gl: globe shader frame=46[0-9][0-9] pixels=358 ' "$tmp/quarter.log"
  if ! awk '$1 == "gl:" && $2 == "globe" && $3 == "parity" &&
            $4 == "frame" && $5 >= 4600 {
              split($7, diff, "/"); seen = 1;
              if (diff[1] > 2) bad = 1;
            } END { exit !(seen && !bad) }' "$tmp/quarter.log"; then
    echo "FAIL: globe GPU parity at bank $bank" >&2
    grep 'gl: globe parity' "$tmp/quarter.log" >&2 || true
    exit 1
  fi
done
echo 'PASS: per-bank native globe survives masking its original bitplanes, with matched pixels and RAM'
