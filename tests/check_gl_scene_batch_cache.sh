#!/usr/bin/env bash
# Verify native GL primitive-stream reuse against forced repacking/upload.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_scene_batch_cache.sh ORIGINAL.adf [fa18-executable]' >&2
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
  flags=(FA18_GL_BATCH_CACHE_DEBUG=1 FA18_GL_DIRECT_COMPARE=1)
  if [ "$mode" = reference ]; then
    flags+=(FA18_GL_BATCH_CACHE_REFERENCE=1)
  fi
  env "${flags[@]}" "$bin" "${args[@]}" "$adf" >"$tmp/$mode.log" 2>&1
  grep -q 'gl: scene batches rebuilt=' "$tmp/$mode.log"
  grep -q 'gl: direct parity frame 3570 different ' "$tmp/$mode.log"
done

cmp <(grep 'gl: direct parity frame ' "$tmp/cached.log") \
    <(grep 'gl: direct parity frame ' "$tmp/reference.log")
cmp <(grep '^\[ramhash\]' "$tmp/cached.log") \
    <(grep '^\[ramhash\]' "$tmp/reference.log")

# A capture commonly lasts several VBLs, so subsequent interpolated phases
# should update only XYZ; the reference re-packs every style every time.
grep -Eq 'gl: scene batches rebuilt=[0-9]+ moved=[1-9][0-9]*' "$tmp/cached.log"
if grep -Eq 'gl: scene batches rebuilt=[0-9]+ moved=[1-9][0-9]*' "$tmp/reference.log"; then
  echo 'FAIL: GL batch reference unexpectedly retained a capture' >&2
  exit 1
fi
echo 'PASS: scene batches retain styles across interpolation; direct GPU parity and RAM unchanged'
