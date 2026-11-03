#!/usr/bin/env bash
# ADF-backed check: analog stick injection must match across the translated,
# recompiled and interpreted $25584 paths, and must actually affect flight.
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
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
args=(--det-timing --fast --frames 3850 --script "$repo/tests/data/analog_flight.txt")

"$bin" --analog --native on "${args[@]}" --ram-dump 3800 "$tmp/native.bin" "$adf" >"$tmp/native.log" 2>&1
"$bin" --analog --native off "${args[@]}" --ram-dump 3800 "$tmp/recompiled.bin" "$adf" >"$tmp/recompiled.log" 2>&1
"$bin" --analog --native off --interpret "${args[@]}" --ram-dump 3800 "$tmp/interpreter.bin" "$adf" >"$tmp/interpreter.log" 2>&1
"$bin" --native off "${args[@]}" --ram-dump 3800 "$tmp/disabled.bin" "$adf" >"$tmp/disabled.log" 2>&1

# The native calling convention can leave different scratch stack bytes below
# $20000, but game RAM (from LOAD_BASE onward) must be identical.
cmp -i 131072 "$tmp/native.bin" "$tmp/recompiled.bin"
cmp -i 131072 "$tmp/interpreter.bin" "$tmp/recompiled.bin"
if cmp -s -i 131072 "$tmp/recompiled.bin" "$tmp/disabled.bin"; then
  echo 'FAIL: analog axes did not affect game RAM' >&2
  exit 1
fi
echo 'PASS: analog input is effective and identical across native/recompiled/interpreter'
