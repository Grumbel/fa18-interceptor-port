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
bash "$here/run_scenarios.sh" "$w/off" --native off --det-timing --ram-hash 50 "$@" > /dev/null
bash "$here/run_scenarios.sh" "$w/on" --native on --det-timing --ram-hash 50 "$@" > /dev/null
bash "$here/compare_runs.sh" "$w/off" "$w/on"
