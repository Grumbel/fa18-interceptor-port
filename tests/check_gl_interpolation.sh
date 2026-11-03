#!/usr/bin/env bash
# Optional ADF-backed GL integration test: more presents, identical game RAM.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "usage: $0 ORIGINAL.adf [fa18-executable]" >&2
  exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.bin "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

# Fast-forward the menus, then run 160 VBLs in real time. The 68000 still
# executes the same number of frames regardless of GL presentation rate.
args=(--gl 1 --widescreen --native off --interpret --det-timing --fast
      --frames 3710 --script "$repo/tests/data/interpolation_flight.txt")
"$bin" "${args[@]}" --render-fps 60 --ram-dump 3700 "$tmp/60.bin" \
  "$adf" >"$tmp/60.log" 2>&1
FA18_GL_INTERP_DEBUG=1 "$bin" "${args[@]}" --render-fps 120 \
  --ram-dump 3700 "$tmp/120.bin" "$adf" >"$tmp/120.log" 2>&1
cmp "$tmp/60.bin" "$tmp/120.bin"
if ! grep -q 'gl: extra presentation' "$tmp/120.log"; then
  echo 'FAIL: 120 Hz requested, but no extra frames were presented' >&2
  exit 1
fi
if ! grep -Eq 'gl: redraws [0-9]+ interpolated [1-9][0-9]* matched-prims [1-9][0-9]*' "$tmp/120.log"; then
  echo 'FAIL: GL did not interpolate matching 3D primitives' >&2
  exit 1
fi
echo 'PASS: extra GL frames interpolate geometry, emulated RAM unchanged at 120 Hz'
