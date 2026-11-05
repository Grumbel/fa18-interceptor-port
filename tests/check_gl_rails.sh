#!/usr/bin/env bash
# Compare native 960x720 HUD verticals to their legacy bitmap rails.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_rails.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

common=(--native off --interpret --det-timing --fast --gl 3
        --widescreen --render-fps 120)
for mode in flight pause; do
  if [ "$mode" = flight ]; then
    frame=4000
    args=(--frames 4010 --script "$repo/tests/scenarios/free1.txt")
  else
    frame=3570
    args=(--frames 3580 --script "$repo/tests/data/pause_gl.txt")
  fi
  FA18_HUD_RAILS_NATIVE=0 "$bin" "${common[@]}" "${args[@]}" \
    --screenshot "$frame" "$tmp/$mode-old.ppm" "$adf" >"$tmp/$mode-old.log" 2>&1
  "$bin" "${common[@]}" "${args[@]}" \
    --screenshot "$frame" "$tmp/$mode-native.ppm" "$adf" >"$tmp/$mode-native.log" 2>&1
  python3 "$repo/tests/check_gl_rails.py" \
    "$tmp/$mode-native.ppm" "$tmp/$mode-old.ppm"
done

# The compositor's diagnostic comparison samples one logical pixel centre
# for every source pixel. At 3x, the old raster edge still differs by two
# unrelated pixels at the centre of the runway; the rails must add none.
FA18_COCKPIT_RADAR_NATIVE=0 FA18_GL_DIRECT_COMPARE=1 "$bin" --native off --interpret --det-timing --fast \
  --gl 3 --gl-direct --widescreen --render-fps 120 --frames 3650 \
  --script "$repo/tests/data/direct_flight.txt" \
  "$adf" >"$tmp/direct.log" 2>&1
python3 - "$tmp/direct.log" <<'PY'
import re
import sys
from pathlib import Path

data = Path(sys.argv[1]).read_text()
samples = re.findall(r'gl: direct parity frame \d+ different (\d+)/85600', data)
assert len(samples) >= 2, 'GPU/CPU rail parity was not sampled'
assert max(map(int, samples)) <= 2, 'native HUD rails add compositor differences'
for x, y in re.findall(r'gl: direct diff at (\d+),(\d+)', data):
    assert not (int(x) in (139, 287) and 45 <= int(y) <= 145), \
        f'GPU/CPU mismatch on native rail {x},{y}'
print(f'PASS: {len(samples)} GPU/CPU pixel-centre comparisons at 3x widescreen')
PY

echo 'PASS: scene-backed native HUD rails bounded in flight/Pause and GPU-parity checked'
