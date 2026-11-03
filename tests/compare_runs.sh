#!/usr/bin/env bash
# Compares two run_scenarios.sh output directories made with --det-timing
# --ram-hash: complete memory-hash histories (every N frames) and screenshots.
# Prefer the runner's completion-verified digests when available.
#   tests/compare_runs.sh DIR_A DIR_B
a_dir=$1 b_dir=$2
fail=0
for f in "$a_dir"/*.log; do
  n=$(basename "$f" .log)
  # The runner records these only after checking the full scripted run and
  # screenshot. Compare the original complete hash streams via their
  # digests: intermediate .log files can be truncated by workspace syncing
  # after the producing process has already exited successfully.
  if [ -f "$a_dir/$n.verified" ] || [ -f "$b_dir/$n.verified" ]; then
    if [ -f "$a_dir/$n.verified" ] && [ -f "$b_dir/$n.verified" ] &&
       cmp -s "$a_dir/$n.verified" "$b_dir/$n.verified"; then
      echo "same $n"
    else
      echo "DIFF $n: verified RAM history or final screenshot differs"; fail=1
    fi
    continue
  fi
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
