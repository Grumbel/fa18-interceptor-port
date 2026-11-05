#!/usr/bin/env bash
# The GL compositor uses the original raster's low-res *coverage*, not its
# redundant software-painted high-res colours. Check every captured mask and
# the resulting pixels with the legacy software coverage path as an oracle.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo 'usage: check_gl_coverage.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.log "$tmp"/*.ppm; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

for scale in 1 4; do
  args=(--native off --interpret --det-timing --fast --full-detail
        --gl "$scale" --gl-direct --widescreen --frames 3550
        --script "$repo/tests/data/direct_flight.txt" --ram-hash 100)
  FA18_GL_COVERAGE_REFERENCE=1 FA18_GL_COVERAGE_DEBUG=1 \
    "$bin" "${args[@]}" --screenshot 3520 "$tmp/reference-$scale.ppm" "$adf" \
    >"$tmp/reference-$scale.log" 2>&1
  FA18_GL_COVERAGE_DEBUG=1 \
    "$bin" "${args[@]}" --screenshot 3520 "$tmp/native-$scale.ppm" "$adf" \
    >"$tmp/native-$scale.log" 2>&1
  test "$(grep -c '^gl: coverage frame=' "$tmp/native-$scale.log")" -gt 100
  cmp <(grep '^gl: coverage frame=' "$tmp/reference-$scale.log") \
      <(grep '^gl: coverage frame=' "$tmp/native-$scale.log")
  cmp <(grep '^\[ramhash\]' "$tmp/reference-$scale.log") \
      <(grep '^\[ramhash\]' "$tmp/native-$scale.log")
  cmp "$tmp/reference-$scale.ppm" "$tmp/native-$scale.ppm"
done
echo 'PASS: GL coverage-only rendering matches legacy masks, RAM and pixels at 1x/4x'
