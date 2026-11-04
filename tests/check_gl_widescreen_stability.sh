#!/usr/bin/env bash
# Inspect consecutive 4:3 -> widescreen compositions during flight.
# In particular, a newly begun but unfinished Amiga back buffer must not
# make the scene alternate with a black/original-resolution frame.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_widescreen_stability.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.txt "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
sed "s|@OUTPUT@|$tmp|g" "$repo/tests/data/widescreen_stability.txt" > "$tmp/replay.txt"

FA18_GL_PRESENT_DEBUG=1 "$bin" --native off --interpret --det-timing --fast \
  --gl 3 --widescreen --render-fps 120 --frames 4521 --script "$tmp/replay.txt" \
  "$adf" >"$tmp/run.log" 2>&1

if grep -Eq '^gl: frame 451[0-9] fell back' "$tmp/run.log"; then
  echo 'FAIL: widescreen returned to the original low-res frame during flight' >&2
  grep '^gl: frame 451' "$tmp/run.log" >&2 || true
  exit 1
fi

python3 - "$tmp" <<'PY'
import pathlib
import sys

folder = pathlib.Path(sys.argv[1])
means = []
for frame in range(4510, 4520):
    with (folder / f"{frame}.ppm").open('rb') as f:
        assert f.readline() == b'P6\n'
        width, height = map(int, f.readline().split())
        assert f.readline() == b'255\n'
        rgb = f.read()
    assert width > 320 * 3 and height == 200 * 3
    assert len(rgb) == width * height * 3
    means.append(sum(rgb) / len(rgb))

if min(means) < 15 or max(means) > 2 * min(means):
    raise SystemExit(f"FAIL: consecutive widescreen frames flicker/blank: {means}")
print(f"PASS: 10 consecutive widescreen frames remain filled ({min(means):.1f}..{max(means):.1f} mean RGB)")
PY
