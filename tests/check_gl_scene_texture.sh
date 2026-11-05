#!/usr/bin/env bash
# Render-to-texture scene FBO vs the previous renderbuffer/copy compositor.
# Tests the 4:3 CPU readback, widescreen, paused copper palette, and a visible
# GL presentation. The RGBA4 renderbuffer remains an explicit compatibility
# fallback even on hosts where texture attachments are supported.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_scene_texture.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

for mode in flight pause; do
  if [ "$mode" = flight ]; then
    frame=4000
    args=(--frames 4010 --script "$repo/tests/scenarios/free1.txt")
  else
    frame=3570
    args=(--frames 3580 --script "$repo/tests/data/pause_gl.txt")
  fi
  common=(--native off --interpret --det-timing --fast --gl 3
          --render-fps 120 --widescreen "${args[@]}" --screenshot "$frame")
  FA18_GL_SCENE_TEXTURE=0 FA18_GL_SCENE_DEBUG=1 \
    "$bin" "${common[@]}" "$tmp/$mode-rb.ppm" "$adf" >"$tmp/$mode-rb.log" 2>&1
  FA18_GL_SCENE_DEBUG=1 \
    "$bin" "${common[@]}" "$tmp/$mode-tex.ppm" "$adf" >"$tmp/$mode-tex.log" 2>&1
  cmp "$tmp/$mode-rb.ppm" "$tmp/$mode-tex.ppm"
  grep -q 'gl: scene FBO .* colour=renderbuffer' "$tmp/$mode-rb.log"
  grep -q 'gl: scene FBO .* colour=texture' "$tmp/$mode-tex.log"
done

FA18_GL_FORCE_RGBA4=1 FA18_GL_SCENE_DEBUG=1 \
  "$bin" --native off --interpret --det-timing --fast --gl 3 \
  --render-fps 120 --widescreen --frames 4010 \
  --script "$repo/tests/scenarios/free1.txt" --screenshot 4000 \
  "$tmp/rgba4.ppm" "$adf" >"$tmp/rgba4.log" 2>&1
cmp "$tmp/flight-tex.ppm" "$tmp/rgba4.ppm"
grep -q 'gl: scene FBO .* colour=renderbuffer' "$tmp/rgba4.log"

# A real visible-GPU frame alternates between the original centre readback
# and full-width scene. The cache must allocate each distinct size *once*.
FA18_GL_SCENE_DEBUG=1 FA18_GL_DIRECT_COMPARE=1 \
  FA18_COCKPIT_RADAR_NATIVE=0 "$bin" --native off --interpret --det-timing --fast --gl 3 --gl-direct \
  --render-fps 120 --widescreen --frames 3650 \
  --script "$repo/tests/data/direct_flight.txt" \
  "$adf" >"$tmp/direct.log" 2>&1
python3 - "$tmp/direct.log" <<'PY'
import re
import sys
from pathlib import Path

log = Path(sys.argv[1]).read_text()
allocations = re.findall(r'gl: scene FBO (\d+x\d+) colour=(\w+)', log)
assert len(allocations) >= 2, 'no separate centre/widescreen targets'
assert len(allocations) == len(set(allocations)), 'framebuffer was reallocated for the same size'
assert all(method == 'texture' for _, method in allocations), 'texture fallback on supported host'
parity = [int(x) for x in re.findall(r'gl: direct parity frame \d+ different (\d+)/85600', log)]
assert len(parity) >= 2 and max(parity) <= 2, f'GPU/CPU scene parity regression: {parity}'
print(f'PASS: {len(allocations)} reusable FBO sizes and {len(parity)} direct pixel comparisons')
PY

echo 'PASS: GL scene texture equals renderbuffer/copy, RGBA4, and cached widescreen GPU presentation'
