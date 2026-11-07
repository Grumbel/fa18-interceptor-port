#!/usr/bin/env bash
# Preserve exact 60 Hz pixels and emulated RAM when changing how the camera
# matrix is interpolated between otherwise identical completed GL scenes.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_camera_interp.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.log "$tmp"/*.ppm; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

args=(--native off --interpret --det-timing --fast --gl 3 --widescreen
      --frames 3710 --script "$repo/tests/data/interpolation_flight.txt"
      --ram-hash 100 --screenshot 3690)
for hz in 60 120; do
  for mode in rotated linear; do
    flags=(FA18_GL_CAMERA_INTERP_DEBUG=1)
    if [ "$mode" = linear ]; then flags+=(FA18_GL_CAMERA_INTERP_REFERENCE=1); fi
    env "${flags[@]}" "$bin" "${args[@]}" --render-fps "$hz" \
      "$tmp/$hz-$mode.ppm" "$adf" >"$tmp/$hz-$mode.log" 2>&1
  done
  cmp <(grep '^\[ramhash\]' "$tmp/$hz-rotated.log") \
      <(grep '^\[ramhash\]' "$tmp/$hz-linear.log")
done

# A real capture boundary is *never* reorthogonalised: original 60 Hz video
# must stay bit-identical regardless of this enhancement being enabled.
cmp "$tmp/60-rotated.ppm" "$tmp/60-linear.ppm"

# In 120 Hz mode extra presentations use rotation interpolation when both
# camera matrices represent proper rotations. Their images may intentionally
# differ from element-wise linear interpolation, but game RAM cannot.
grep -q 'gl: camera interpolation rotations=' "$tmp/120-rotated.log"
grep -Eq 'gl: camera interpolation rotations=[1-9][0-9]*' "$tmp/120-rotated.log"
echo 'PASS: rotation interpolation active; 60 Hz pixels exact and 120 Hz game RAM unchanged'
