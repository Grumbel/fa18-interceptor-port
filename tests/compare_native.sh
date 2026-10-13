#!/usr/bin/env bash
# Whole-program equivalence check of the native C++ translation against the
# original code: runs every scenario with --native off and --native on in
# deterministic timing mode and compares memory hashes (every 50 frames)
# and the final screenshots.
#   tests/compare_native.sh WORKDIR [extra fa18 options]
set -e
w=$1; shift
here=$(cd "$(dirname "$0")" && pwd)
rm -rf "$w/off" "$w/on"
"$here/run_scenarios.sh" "$w/off" --native off --det-timing --ram-hash 50 "$@" > /dev/null
"$here/run_scenarios.sh" "$w/on" --native on --det-timing --ram-hash 50 "$@" > /dev/null
fail=0
for f in "$w"/off/*.log; do
  n=$(basename "$f" .log)
  a=$(grep '^\[ramhash\]' "$f"); b=$(grep '^\[ramhash\]' "$w/on/$n.log")
  if [ "$a" != "$b" ]; then
    first=$(diff <(echo "$a") <(echo "$b") | grep '^<' | head -1 | awk '{print $4}')
    echo "DIFF $n: memory differs from frame $first"; fail=1
  elif ! cmp -s "$w/off/$n.ppm" "$w/on/$n.ppm"; then
    echo "DIFF $n: final frame differs"; fail=1
  else
    echo "same $n"
  fi
done
exit $fail
