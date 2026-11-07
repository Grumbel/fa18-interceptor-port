#!/usr/bin/env bash
# Read-only selective bitplane masking: same deterministic flight, same RAM.
# Report which output pixels depend on the chosen original Amiga region.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_scanout_region.sh ORIGINAL.adf [region] [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
region=${2:-globe}
bin=${3:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.log "$tmp"/*.ppm; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
args=(--native off --interpret --det-timing --fast --gl 3 --widescreen
      --render-fps 120 --frames 4010 --ram-hash 100
      --script "$repo/tests/scenarios/free1.txt" --screenshot 4000)
"$bin" "${args[@]}" "$tmp/original.ppm" "$adf" >"$tmp/original.log" 2>&1
"$bin" "${args[@]}" "$tmp/masked.ppm" --hide-amiga-region "$region" "$adf" \
  >"$tmp/masked.log" 2>&1
grep -Fq "graphics: original Amiga scanout region $region masked (--hide-amiga-region)" \
  "$tmp/masked.log"
grep -q '^\[ramhash\]' "$tmp/original.log"
cmp <(grep '^\[ramhash\]' "$tmp/original.log") \
    <(grep '^\[ramhash\]' "$tmp/masked.log")
test -s "$tmp/original.ppm"
test -s "$tmp/masked.ppm"
python3 "$repo/tools/scanout_diff.py" "$tmp/original.ppm" "$tmp/masked.ppm"
echo "PASS: selective $region mask preserves guest RAM; inspect changed pixels above"
