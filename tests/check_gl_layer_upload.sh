#!/usr/bin/env bash
# Extra 120-Hz GL presents re-render the interpolated scene but must reuse
# the unchanged Amiga 2D overlay, copper palette and row mapping textures.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: check_gl_layer_upload.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

args=(--native off --interpret --det-timing --fast --gl 1 --gl-direct
      --widescreen --render-fps 120 --frames 3590 --ram-hash 100
      --script "$repo/tests/data/pause_direct_gl.txt")
for mode in cached reference; do
  flags=(FA18_GL_LAYER_UPLOAD_DEBUG=1 FA18_GL_DIRECT_COMPARE=1
         FA18_GL_DIRECT_DEBUG=1
         FA18_GL_INTERP_DEBUG=1)
  if [ "$mode" = reference ]; then
    flags+=(FA18_GL_LAYER_UPLOAD_REFERENCE=1)
  fi
  env "${flags[@]}" "$bin" "${args[@]}" "$adf" >"$tmp/$mode.log" 2>&1
  grep -q 'gl: direct GPU presentation' "$tmp/$mode.log"
  grep -q 'gl: direct parity frame 3570 different ' "$tmp/$mode.log"
  grep -q 'gl: extra presentation' "$tmp/$mode.log"
done
# At 1x the vector Pause heading has a small known coverage difference from
# the software pixel-centre oracle; it must be bounded and identical with
# cache enabled or disabled. The existing 3x direct test checks tight parity.
cmp <(grep 'gl: direct parity frame 3570 different ' "$tmp/cached.log") \
    <(grep 'gl: direct parity frame 3570 different ' "$tmp/reference.log")
awk '$1 == "gl:" && $2 == "direct" && $3 == "parity" && $5 == 3570 {
       split($7, diff, "/"); seen = 1; if (diff[1] > 200) bad = 1;
     } END { exit !(seen && !bad) }' "$tmp/cached.log"
grep -q 'gl: layer textures .*upload=0 ' "$tmp/cached.log"
if grep -q 'gl: layer textures .*upload=0 ' "$tmp/reference.log"; then
  echo 'FAIL: reference mode reused a layer upload' >&2
  exit 1
fi
cmp <(grep '^\[ramhash\]' "$tmp/cached.log") \
    <(grep '^\[ramhash\]' "$tmp/reference.log")
echo 'PASS: extra GL presents reuse 2D textures without changing GPU parity or RAM'
