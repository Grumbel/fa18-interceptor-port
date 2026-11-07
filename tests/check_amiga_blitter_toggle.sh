#!/usr/bin/env bash
# Compare a normal replay with temporary suppression of original blitter
# destination writes. The game must keep progressing and the Amiga RAM hash
# must show that the blitter's original framebuffer writes were suppressed.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_amiga_blitter_toggle.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
args=(--native off --interpret --det-timing --fast --gl 3
      --frames 430 --ram-hash 100)
"$bin" "${args[@]}" "$adf" >"$tmp/original.log" 2>&1
"$bin" "${args[@]}" --script "$repo/tests/data/blitter_toggle.txt" "$adf" \
  >"$tmp/toggled.log" 2>&1
grep -q '^graphics: original Amiga blitter writes OFF ' "$tmp/toggled.log"
grep -q '^graphics: original Amiga blitter writes ON ' "$tmp/toggled.log"
grep -Eq '^blitter-debug: suppressed [1-9][0-9]* destination words' "$tmp/toggled.log"
grep -q '^\[ramhash\]' "$tmp/original.log"
grep -q '^\[ramhash\]' "$tmp/toggled.log"
if cmp -s <(grep '^\[ramhash\]' "$tmp/original.log") \
          <(grep '^\[ramhash\]' "$tmp/toggled.log"); then
  echo 'FAIL: disabling the blitter left the original RAM history unchanged' >&2
  exit 1
fi
echo 'PASS: blitter writes are suppressed and can be restored during a running game'
