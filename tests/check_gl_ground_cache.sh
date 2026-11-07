#!/usr/bin/env bash
# Compare the separately cached ground/water vertex buffer with the previous
# rebuild/upload-every-presentation implementation through flight and Pause.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_ground_cache.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

args=(--native off --interpret --det-timing --fast --gl 3 --gl-direct
      --widescreen --render-fps 120 --frames 3590 --ram-hash 100
      --script "$repo/tests/data/pause_direct_gl.txt")
for mode in cached reference; do
  flags=(FA18_GL_GROUND_CACHE_DEBUG=1 FA18_GL_DIRECT_COMPARE=1)
  if [ "$mode" = reference ]; then
    flags+=(FA18_GL_GROUND_CACHE_REFERENCE=1)
  fi
  env "${flags[@]}" "$bin" "${args[@]}" "$adf" >"$tmp/$mode.log" 2>&1
  grep -q 'gl: ground stream uploaded=' "$tmp/$mode.log"
  grep -q 'gl: direct parity frame 3570 different ' "$tmp/$mode.log"
done

cmp <(grep 'gl: direct parity frame ' "$tmp/cached.log") \
    <(grep 'gl: direct parity frame ' "$tmp/reference.log")
cmp <(grep '^\[ramhash\]' "$tmp/cached.log") \
    <(grep '^\[ramhash\]' "$tmp/reference.log")

# The reference path intentionally re-uploads terrain, even for identical
# poses. In the cached run no more uploads than the reference are permitted.
counts() {
  sed -n 's/.*ground stream uploaded=\([0-9]*\) reused=\([0-9]*\).*/\1 \2/p' "$1" | tail -1
}
read -r cached_upload cached_reuse < <(counts "$tmp/cached.log")
read -r reference_upload reference_reuse < <(counts "$tmp/reference.log")
if [ "$cached_upload" -gt "$reference_upload" ] || [ "$reference_reuse" -ne 0 ]; then
  echo 'FAIL: ground buffer cache counters are inconsistent' >&2
  exit 1
fi
echo "PASS: ground GL buffer uploads $cached_upload/$reference_upload; GPU parity and RAM unchanged"
