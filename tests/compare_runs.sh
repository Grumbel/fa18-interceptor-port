#!/usr/bin/env bash
# Compares two run_scenarios.sh output directories made with --det-timing
# --ram-hash: memory hashes (every N frames) and final screenshots.
#   tests/compare_runs.sh DIR_A DIR_B
a_dir=$1 b_dir=$2
fail=0
for f in "$a_dir"/*.log; do
  n=$(basename "$f" .log)
  a=$(grep '^\[ramhash\]' "$f"); b=$(grep '^\[ramhash\]' "$b_dir/$n.log")
  if [ -z "$a" ]; then
    echo "DIFF $n: no ramhash output"; fail=1
  elif [ "$a" != "$b" ]; then
    first=$(diff <(echo "$a") <(echo "$b") | grep '^<' | head -1 | awk '{print $4}')
    echo "DIFF $n: memory differs from frame $first"; fail=1
  elif ! cmp -s "$a_dir/$n.ppm" "$b_dir/$n.ppm"; then
    echo "DIFF $n: final frame differs"; fail=1
  else
    echo "same $n"
  fi
done
exit $fail
