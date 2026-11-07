#!/usr/bin/env bash
# Read-only attribution of guest CPU/HLE cockpit writes, beyond BLTSIZE
# copies. The observer must not alter screenshots or emulated RAM.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_cockpit_cpu_trace.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.ppm "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
args=(--native off --interpret --det-timing --fast --gl 3 --widescreen
      --render-fps 120 --frames 4010 --ram-hash 100
      --script "$repo/tests/scenarios/free1.txt" --screenshot 4000)
"$bin" "${args[@]}" "$tmp/original.ppm" "$adf" \
  >"$tmp/original.log" 2>&1
FA18_COCKPIT_CPU_TRACE=1 "$bin" "${args[@]}" \
  "$tmp/observed.ppm" "$adf" >"$tmp/observed.log" 2>&1
cmp "$tmp/original.ppm" "$tmp/observed.ppm"
grep -q '^\[ramhash\]' "$tmp/original.log"
cmp <(grep '^\[ramhash\]' "$tmp/original.log") \
    <(grep '^\[ramhash\]' "$tmp/observed.log")
grep -Eq '^cockpit-cpu: attributed sites=[0-9]+ \(top 8 per zone by touched bytes\)' \
  "$tmp/observed.log"
for name in left-attitude globe status; do
  grep -Eq "^cockpit-cpu: zone=$name sites=[0-9]+ touched_bytes=[0-9]+" \
    "$tmp/observed.log"
done
grep '^cockpit-cpu:' "$tmp/observed.log"
echo 'PASS: cockpit CPU/HLE attribution leaves game pixels and RAM unchanged'
