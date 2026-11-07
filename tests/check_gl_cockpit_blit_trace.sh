#!/usr/bin/env bash
# Attribute writes by the original blitter to cockpit instrument pixels.
# The observer must not alter game memory, screenshots, or guest timing.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_cockpit_blit_trace.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
args=(--native off --interpret --det-timing --fast --gl 3 --widescreen
      --render-fps 120 --frames 4010 --ram-hash 100
      --script "$repo/tests/scenarios/free1.txt" --screenshot 4000)
"$bin" "${args[@]}" "$tmp/original.ppm" "$adf" \
  >"$tmp/original.log" 2>&1
FA18_COCKPIT_BLIT_TRACE=1 "$bin" "${args[@]}" \
  "$tmp/traced.ppm" "$adf" >"$tmp/traced.log" 2>&1
cmp "$tmp/original.ppm" "$tmp/traced.ppm"
grep -q '^\[ramhash\]' "$tmp/original.log"
cmp <(grep '^\[ramhash\]' "$tmp/original.log") \
    <(grep '^\[ramhash\]' "$tmp/traced.log")
grep -Eq '^cockpit-blit: attributed sites=[0-9]+ \(top 8 per zone by changed bits\)' \
  "$tmp/traced.log"
for name in left-attitude globe status; do
  grep -Eq "^cockpit-blit: zone=$name sites=[0-9]+ changed_bits=[0-9]+" \
    "$tmp/traced.log"
done
if grep -q '^cockpit-blit: attributed sites=0 ' "$tmp/traced.log"; then
  echo 'NOTE: original blitter touched no watched panel zones; examine CPU writes next' >&2
else
  grep -Eq '^cockpit-blit: zone=(left-attitude|globe|status) pc=[[:xdigit:]]+ ' \
    "$tmp/traced.log"
fi
grep '^cockpit-blit:' "$tmp/traced.log"
echo 'PASS: original cockpit blitter trace preserves framebuffer and guest RAM'
