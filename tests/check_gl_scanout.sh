#!/usr/bin/env bash
# Compare a complete scripted flight with and without original Amiga
# bitplane scanout. This mode must keep *all* guest writes and RAM hashes.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_scanout.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.log "$tmp"/*.ppm; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
args=(--native off --interpret --det-timing --fast --gl 3 --widescreen
      --render-fps 120 --frames 4010 --ram-hash 100
      --script "$repo/tests/scenarios/free1.txt" --screenshot 4000)
"$bin" "${args[@]}" "$tmp/original.ppm" "$adf" >"$tmp/original.log" 2>&1
"$bin" "${args[@]}" "$tmp/hidden.ppm" --no-amiga-scanout "$adf" \
  >"$tmp/hidden.log" 2>&1
grep -q '^graphics: original Amiga bitplane scanout OFF (--no-amiga-scanout)' \
  "$tmp/hidden.log"
grep -q '^\[ramhash\]' "$tmp/original.log"
cmp <(grep '^\[ramhash\]' "$tmp/original.log") \
    <(grep '^\[ramhash\]' "$tmp/hidden.log")
test -s "$tmp/original.ppm"
test -s "$tmp/hidden.ppm"
if cmp -s "$tmp/original.ppm" "$tmp/hidden.ppm"; then
  echo 'FAIL: masking the bitplane scanout did not affect the cockpit' >&2
  exit 1
fi
python3 "$repo/tools/scanout_diff.py" "$tmp/original.ppm" "$tmp/hidden.ppm"
echo 'PASS: bitplane visibility differs while the guest RAM history is identical'
