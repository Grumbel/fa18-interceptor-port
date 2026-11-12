#!/usr/bin/env bash
# Integration test of --vr without a headset (needs the user's original ADF
# and an offscreen OpenGL context; not part of the fast ctest run).
#
# FA18_VR_SIM gives a fixed head and puts both eyes' pictures side by side
# into the window.
#
#  1. Head straight: every pixel of the original picture is seen in the
#     direction the original's projection gives it (sky, ground, HUD and
#     instrument panel), in both eyes.
#  2. Head turned: the view turns the other way; the panel leaves the
#     picture, sky and ground remain.
#  3. The game is not affected: the RAM hashes equal the original renderer's.
#
# The OpenXR session itself is not covered; try it with any runtime, e.g.
# Monado's simulated headset (docs/vr.md).
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "usage: $0 ORIGINAL.adf [fa18-executable]" >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

args=(--det-timing --fast --frames 4010 --script "$repo/tests/scenarios/free1.txt" --ram-hash 50)
FA18_VR_SIM=0,0,0,90,640 "$bin" --vr "${args[@]}" --screenshot 4000 "$tmp/ahead.ppm" "$adf" >"$tmp/ahead.log" 2>&1
FA18_VR_SIM=90,0,0,90,640 "$bin" --vr "${args[@]}" --screenshot 4000 "$tmp/right.ppm" "$adf" >"$tmp/right.log" 2>&1
FA18_VR_SIM=0,60,0,90,640 "$bin" --vr "${args[@]}" --screenshot 4000 "$tmp/up.ppm" "$adf" >"$tmp/up.log" 2>&1
"$bin" --renderer original "${args[@]}" --screenshot 4000 "$tmp/orig.ppm" "$adf" >"$tmp/orig.log" 2>&1

python3 - "$tmp/orig.ppm" "$tmp/ahead.ppm" "$tmp/right.ppm" "$tmp/up.ppm" <<'EOF2'
import sys

def ppm(path):
    data = open(path, 'rb').read()
    parts = data.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    return w, h, lambda x, y: tuple(px[(y * w + x) * 3:(y * w + x) * 3 + 3])

ow, oh, orig = ppm(sys.argv[1])
assert (ow, oh) == (320, 200), (ow, oh)
w, h, ahead = ppm(sys.argv[2])
_, _, right = ppm(sys.argv[3])
_, _, up = ppm(sys.argv[4])
E = 640                      # an eye's picture, 90 degrees wide and high
assert w == 2 * E and h >= E, f'window is {w}x{h}'
top = (h - E) // 2

def eye_pixel(x, y):
    """Centre of the original's pixel (x, y) in an eye that looks straight ahead."""
    cx = (x + 0.5) / 160 - 1                 # the original's clip coordinates
    cy = 1 - (y + 0.5) / 100
    tan_right = cx * 128 / 168               # row scales 168, 252, 128 in flight
    tan_up = (cy - 0.105) / 0.9 * 128 / 252
    return int((tan_right + 1) / 2 * E), top + int((1 - tan_up) / 2 * E)

sky = orig(20, 10)
ground = orig(20, 120)
assert sky != ground
bad = []
for eye in (0, 1):
    # sky and ground (the panel's small pixels are compared in bulk below)
    for x, y in [(20, 10), (300, 20), (160, 5), (20, 120), (300, 125)]:
        ex, ey = eye_pixel(x, y)
        if ahead(eye * E + ex, ey) != orig(x, y):
            bad.append((eye, x, y, ahead(eye * E + ex, ey), orig(x, y)))
if bad:
    sys.exit(f'FAIL: head straight: pixels differ from the original picture: {bad}')
same = total = 0
for y in range(150, 199):
    for x in range(1, 319):
        # an original pixel is 1.5 pixels here: only those within one colour
        c = orig(x, y)
        if any(orig(x + dx, y + dy) != c for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
            continue
        ex, ey = eye_pixel(x, y)
        total += 1
        same += ahead(ex, ey) == c
if total < 2000 or same < total * 0.97:
    sys.exit(f'FAIL: instrument panel matches only {same}/{total} pixels')
# beyond the original's picture: sky above, the console below the panel
if ahead(E // 2, top + 5) != sky:
    sys.exit('FAIL: no sky above the original picture')
if ahead(E // 2, top + E - 5) != (0, 0, 0):
    sys.exit('FAIL: no console below the instrument panel')
# both eyes show the same (everything is at infinity)
diff = sum(ahead(x, y) != ahead(E + x, y) for y in range(top, top + E, 7) for x in range(0, E, 7))
if diff:
    sys.exit(f'FAIL: the eyes differ in {diff} sampled pixels')

# head turned right by 90 degrees: horizon in the middle, no panel
ex, ey = eye_pixel(60, 170)
panel = sum(right(x, y) == ahead(x, y) and ahead(x, y) not in (sky, ground, (0, 0, 0))
            for y in range(top + E // 2 + 60, top + E, 5) for x in range(0, E, 5))
if right(E // 2, top + 40) != sky or right(E // 2, top + E - 40) == sky or panel > 50:
    sys.exit(f'FAIL: head turned right: sky {right(E // 2, top + 40)}, panel pixels {panel}')
# head up by 60 degrees: only sky
if any(up(x, top + y) != sky for x in (10, E // 2, E - 10) for y in (10, E // 2, E - 10)):
    sys.exit('FAIL: head up: not only sky')
print(f'head straight: panel {same}/{total} pixels as the original; turned and raised head ok')
EOF2

for run in ahead right up; do
  if ! diff <(grep -a 'ram hash\|\[hash\]' "$tmp/$run.log") <(grep -a 'ram hash\|\[hash\]' "$tmp/orig.log") >/dev/null; then
    echo "FAIL: --vr changed the game's memory ($run)" >&2
    exit 1
  fi
done
if ! grep -aq 'hash' "$tmp/ahead.log"; then
  echo "FAIL: no RAM hashes were logged" >&2
  exit 1
fi
echo "PASS"
