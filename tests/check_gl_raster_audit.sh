#!/usr/bin/env bash
# The opt-in dependency observer must be behaviour-preserving in a real
# flight, while reporting uses of the original blitter's scene bitplanes.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: check_gl_raster_audit.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.log "$tmp"/*.ppm; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
args=(--native off --interpret --det-timing --fast --gl 1 --frames 3550
      --script "$repo/tests/data/direct_flight.txt" --ram-hash 100)
"$bin" "${args[@]}" --screenshot 3520 "$tmp/plain.ppm" "$adf" >"$tmp/plain.log" 2>&1
FA18_GL_RASTER_AUDIT=1 "$bin" "${args[@]}" \
  --screenshot 3520 "$tmp/audit.ppm" "$adf" >"$tmp/audit.log" 2>&1
cmp "$tmp/plain.ppm" "$tmp/audit.ppm"
cmp <(grep '^\[ramhash\]' "$tmp/plain.log") <(grep '^\[ramhash\]' "$tmp/audit.log")
grep -q 'gl-raster-audit: scenes=[1-9]' "$tmp/audit.log"
grep -Eq 'gl-raster-audit: scenes=[1-9].*scene_blitter_words=[1-9]' "$tmp/audit.log"
grep -Eq 'gl-raster-audit: tagged reads blitter +during=[1-9]' "$tmp/audit.log"
grep -q 'gl-raster-audit: tagged reads display DMA' "$tmp/audit.log"
grep -q 'gl-raster-audit: candidate read pc=' "$tmp/audit.log"
grep '^gl-raster-audit:' "$tmp/audit.log"
echo 'PASS: raster dependency observer changes neither RAM hashes nor pixels'
