#!/usr/bin/env bash
# The HUD/cockpit GL uniforms and immutable fullscreen quad must not be rebuilt
# for each interpolated 120-Hz presentation. Compare cached uniforms against
# the explicit every-presentation reference, including a flight/Pause fade.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_present_cache.sh ORIGINAL.adf [fa18-executable]' >&2
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
  flags=(FA18_GL_PRESENT_CACHE_DEBUG=1 FA18_GL_DIRECT_COMPARE=1
         FA18_GL_INTERP_DEBUG=1)
  if [ "$mode" = reference ]; then
    flags+=(FA18_GL_PRESENT_CACHE_REFERENCE=1)
  fi
  env "${flags[@]}" "$bin" "${args[@]}" "$adf" >"$tmp/$mode.log" 2>&1
  grep -q 'gl: extra presentation' "$tmp/$mode.log"
  grep -q 'gl: direct parity frame 3570 different ' "$tmp/$mode.log"
  grep -q 'gl: present HUD uniforms' "$tmp/$mode.log"
  grep -q 'gl: present cockpit uniforms' "$tmp/$mode.log"
done

# At least one extra presentation must have reused HUD uniforms. In the
# reference every presentation uploads them, even if values have not changed.
grep -Eq 'gl: present HUD uniforms updated=[0-9]+ reused=[1-9][0-9]*' "$tmp/cached.log"
grep -Eq 'gl: present cockpit uniforms updated=[0-9]+ reused=[1-9][0-9]*' "$tmp/cached.log"
if grep -Eq 'gl: present HUD uniforms updated=[0-9]+ reused=[1-9][0-9]*' "$tmp/reference.log"; then
  echo 'FAIL: HUD reference unexpectedly reused cached uniforms' >&2
  exit 1
fi
if grep -Eq 'gl: present cockpit uniforms updated=[0-9]+ reused=[1-9][0-9]*' "$tmp/reference.log"; then
  echo 'FAIL: cockpit reference unexpectedly reused cached uniforms' >&2
  exit 1
fi
cmp <(grep 'gl: direct parity frame ' "$tmp/cached.log") \
    <(grep 'gl: direct parity frame ' "$tmp/reference.log")
cmp <(grep '^\[ramhash\]' "$tmp/cached.log") \
    <(grep '^\[ramhash\]' "$tmp/reference.log")
echo 'PASS: cached HUD uniforms preserve direct GPU parity and emulated RAM'
