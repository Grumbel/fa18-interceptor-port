#!/usr/bin/env bash
# Integration test of --renderer new (needs the user's original ADF and an
# offscreen OpenGL context; not part of the fast ctest run).
#
#  1. The shadow machine reproduces the game's own drawing: with
#     FA18_SHADOW_VERIFY=1 the primitives of every 3D frame are compared
#     with what the game drew.  View changes differ for one frame; more
#     than 3% differing frames fails.
#  2. The new renderer draws the 3D view, the cockpit and the HUD: a
#     window screenshot has the sky, the ground, the panel and HUD ink
#     where the original has them.
#  3. Enabling it does not change the game: the RAM hashes of a run equal
#     those of the original renderer.
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

for sc in free1 mission7_combat qual_carrier; do
  frames=$(awk '/quit/{n=$1} END{print n+0}' "$repo/tests/scenarios/$sc.txt")
  FA18_SHADOW_VERIFY=1 "$bin" --renderer original --det-timing --fast --frames "$frames" \
    --script "$repo/tests/scenarios/$sc.txt" "$adf" >"$tmp/verify-$sc.log" 2>&1 || true
  line=$(grep 'shadow: verified' "$tmp/verify-$sc.log" | tail -1)
  if [ -z "$line" ]; then
    echo "FAIL: $sc: the shadow machine verified no frame" >&2
    exit 1
  fi
  verified=$(echo "$line" | awk '{print $3}')
  differ=$(echo "$line" | awk '{print $5}')
  echo "$sc: $verified frames verified, $differ differ"
  if [ "$verified" -lt 500 ] || [ $((differ * 100)) -gt $((verified * 3)) ]; then
    echo "FAIL: $sc: shadow primitives differ from the game's" >&2
    exit 1
  fi
done

args=(--det-timing --fast --frames 4010 --script "$repo/tests/scenarios/free1.txt")
"$bin" --renderer new "${args[@]}" --ram-hash 50 --screenshot 4000 "$tmp/new.ppm" "$adf" \
  >"$tmp/new.log" 2>&1
"$bin" --renderer original "${args[@]}" --ram-hash 50 --screenshot 4000 "$tmp/orig.ppm" "$adf" \
  >"$tmp/orig.log" 2>&1
python3 - "$tmp/new.ppm" "$tmp/orig.ppm" <<'EOF'
import sys

def ppm(path):
    data = open(path, 'rb').read()
    parts = data.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]

w, h, new = ppm(sys.argv[1])
ow, oh, orig = ppm(sys.argv[2])
assert (ow, oh) == (320, 200), (ow, oh)
assert w * 3 >= h * 4 and h >= 400, f'window screenshot is {w}x{h}'

def at(x, y):          # new picture
    i = (y * w + x) * 3
    return tuple(new[i:i + 3])

def orig_at(x, y):
    i = (y * 320 + x) * 3
    return tuple(orig[i:i + 3])

# The original's 4:3 picture sits in the middle at full height.
cw = h * 4 // 3
ox = (w - cw) // 2
def centre(x, y):      # original pixel centre -> new picture
    return at(ox + int((x + 0.5) * cw / 320), int((y + 0.5) * h / 200))

bad = []
# sky, ground, and pixels inside the instrument panel
for x, y in [(20, 10), (300, 20), (20, 120), (160, 195), (60, 170), (290, 185)]:
    if centre(x, y) != orig_at(x, y):
        bad.append((x, y, centre(x, y), orig_at(x, y)))
if bad:
    sys.exit(f'FAIL: pixels differ from the original picture: {bad}')
# The whole panel: the same colours for almost all pixel centres.
same = total = 0
for y in range(150, 200):
    for x in range(0, 320):
        total += 1
        same += centre(x, y) == orig_at(x, y)
if same < total * 0.97:
    sys.exit(f'FAIL: instrument panel matches only {same}/{total} pixels')
# The 3D view continues beside the 4:3 centre in a wide window.
if ox > 8 and at(2, 5) != centre(20, 10):
    sys.exit('FAIL: no sky beside the centre of a wide window')
print(f'picture: {w}x{h}, panel {same}/{total} pixels as the original')
EOF

if ! diff <(grep -a 'ram hash\|\[hash\]' "$tmp/new.log") <(grep -a 'ram hash\|\[hash\]' "$tmp/orig.log") >/dev/null; then
  echo "FAIL: --renderer new changed the game's memory" >&2
  exit 1
fi
if ! grep -aq 'hash' "$tmp/new.log"; then
  echo "FAIL: no RAM hashes were logged" >&2
  exit 1
fi
echo "PASS"
