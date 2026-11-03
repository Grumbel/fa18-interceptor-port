#!/usr/bin/env bash
# Optional integration test (requires the user's original ADF and a working
# offscreen GL context). Runs real game scripts; not part of fast ctest.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "usage: $0 ORIGINAL.adf [fa18-executable]" >&2
  exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
unset FA18_GL_FORCE_RGBA4

args=(--interpret --det-timing --fast --frames 4010
      --script "$repo/tests/scenarios/free1.txt")

FA18_GL_DEBUG=1 "$bin" --native off --gl 3 "${args[@]}" \
  --screenshot 4000 "$tmp/gl-interpreter.ppm" "$adf" >"$tmp/gl.log" 2>&1
if ! grep -Eq 'gl: frame [0-9]+ objects [1-9][0-9]* prims [1-9][0-9]*' "$tmp/gl.log"; then
  echo 'FAIL: interpreter initialized GL but never captured scene objects' >&2
  exit 1
fi

"$bin" --native on --gl 3 "${args[@]}" \
  --screenshot 4000 "$tmp/gl-native.ppm" "$adf" >"$tmp/native.log" 2>&1
cmp "$tmp/gl-interpreter.ppm" "$tmp/gl-native.ppm"

# Without --interpret, a recomp-enabled build executes multi-instruction
# chunks. They must return to the dispatcher before every GL capture hook.
FA18_GL_DEBUG=1 "$bin" --native off --gl 3 --det-timing --fast --frames 4010 \
  --script "$repo/tests/scenarios/free1.txt" \
  --screenshot 4000 "$tmp/gl-recompiled.ppm" "$adf" >"$tmp/recompiled.log" 2>&1
if ! grep -Eq 'gl: frame [0-9]+ objects [1-9][0-9]* prims [1-9][0-9]*' "$tmp/recompiled.log"; then
  echo 'FAIL: recompiled GL bypassed scene capture hooks' >&2
  exit 1
fi
cmp "$tmp/gl-interpreter.ppm" "$tmp/gl-recompiled.ppm"

"$bin" --native off --hires 3 "${args[@]}" \
  --screenshot 4000 "$tmp/software.ppm" "$adf" >"$tmp/software.log" 2>&1
if cmp -s "$tmp/gl-interpreter.ppm" "$tmp/software.ppm"; then
  echo 'FAIL: OpenGL screenshot is identical to the software renderer' >&2
  exit 1
fi

FA18_GL_FORCE_RGBA4=1 "$bin" --native off --gl 3 "${args[@]}" \
  --screenshot 4000 "$tmp/gl-rgba4.ppm" "$adf" >"$tmp/rgba4.log" 2>&1
cmp "$tmp/gl-interpreter.ppm" "$tmp/gl-rgba4.ppm"

"$bin" --native off --gl 3 --widescreen "${args[@]}" \
  --screenshot 4000 "$tmp/gl-widescreen.ppm" "$adf" >"$tmp/widescreen.log" 2>&1
python3 "$repo/tests/compare_ppm_center.py" \
  "$tmp/gl-interpreter.ppm" "$tmp/gl-widescreen.ppm"

echo 'PASS: GL interpreter/native/recompiled agree, GL differs from software, RGBA4 == RGBA8'
