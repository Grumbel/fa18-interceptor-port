#!/usr/bin/env bash
# Ensure the optional changing-panel classifier has no effect on simulation or
# rendering; it describes original ADF/bitplane differences for RE only.
set -euo pipefail
if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_cockpit_dynamic_audit.sh ORIGINAL.adf [fa18-executable]' >&2
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
FA18_COCKPIT_DYNAMIC_AUDIT=1 "$bin" "${args[@]}" \
  "$tmp/audit.ppm" "$adf" >"$tmp/audit.log" 2>&1
cmp "$tmp/original.ppm" "$tmp/audit.ppm"
cmp <(grep '^\[ramhash\]' "$tmp/original.log") \
    <(grep '^\[ramhash\]' "$tmp/audit.log")
for name in left-attitude fuel radar globe readouts status; do
  grep -Eq "^cockpit dynamic CPU frame=4000 bank=[[:xdigit:]]+ zone=$name sample=[1-9][0-9]* " \
    "$tmp/audit.log" || {
      echo "FAIL: no changing-panel audit data for $name" >&2
      exit 1
    }
done
echo 'PASS: six original cockpit dynamic regions audited without pixel/RAM changes'
