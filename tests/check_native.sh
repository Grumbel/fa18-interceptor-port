#!/usr/bin/env bash
# Gate for native C++ translations. Must pass before every commit that adds
# or changes code in src/native/:
#   1. whole-program equivalence (tests/compare_native.sh): all scenarios
#      give identical memory every 50 frames with --native on and off
#   2. per-call verification (--native verify) of every routine marked
#      verifiable: no mismatches
#   tests/check_native.sh [WORKDIR]
set -e
here=$(cd "$(dirname "$0")" && pwd)
w=${1:-$(mktemp -d)}
mkdir -p "$w"
echo "== equivalence (compare_native.sh)"
if ! "$here/compare_native.sh" "$w/cmp" > "$w/compare.txt"; then
  grep DIFF "$w/compare.txt"; echo "FAIL: native code is not equivalent"; exit 1
fi
echo "   $(grep -c same "$w/compare.txt") scenarios identical"
echo "== per-call verify"
rm -rf "$w/verify"
"$here/run_scenarios.sh" "$w/verify" --native verify --det-timing > "$w/verify.txt" 2>&1 || true
if grep -q "MISMATCH" "$w/verify.txt" || grep -q "FAILED" "$w/verify.txt"; then
  grep "MISMATCH\|FAILED" "$w/verify.txt" | head -20; echo "FAIL: verify mismatches"; exit 1
fi
echo "   no mismatches"
echo "PASS"
