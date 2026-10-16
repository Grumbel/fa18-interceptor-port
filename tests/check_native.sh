#!/usr/bin/env bash
# Gate for native C++ translations. Must pass before every commit that adds
# or changes code in src/native/:
#   1. whole-program equivalence (tests/compare_native.sh): all scenarios
#      give identical memory every 50 frames with --native on and off
#   2. per-call verification (--native verify) of every routine marked
#      verifiable: no mismatches
#   3. if the build has recompiled code (so "--native off" in 1. ran the
#      recompiler): the 68000 interpreter (--interpret) gives the same
#      memory as the recompiled code. FA18_SKIP_INTERP=1 skips this step.
#   tests/check_native.sh [WORKDIR]
set -e
here=$(cd "$(dirname "$0")" && pwd)
w=${1:-$(mktemp -d)}
mkdir -p "$w"
echo "== build"
here_root=$(cd "$here/.." && pwd)
if [ -n "${FA18_SKIP_BUILD:-}" ] && [ -x "$here_root/build/fa18" ]; then
  echo "   skipped (FA18_SKIP_BUILD, using $here_root/build/fa18)"
elif ! cmake --build "$here_root/build" -j"$(nproc)" > "$w/build.txt" 2>&1; then
  grep -E "error|Error" "$w/build.txt" | head -20; echo "FAIL: build failed"; exit 1
fi
echo "== equivalence (compare_native.sh)"
if ! bash "$here/compare_native.sh" "$w/cmp" > "$w/compare.txt"; then
  grep DIFF "$w/compare.txt"; echo "FAIL: native code is not equivalent"; exit 1
fi
echo "   $(grep -c same "$w/compare.txt") scenarios identical"
engine=$(grep -h "^running " "$w"/cmp/off/*.log | sort -u)
echo "   original code ran as: $engine"
echo "== per-call verify"
rm -rf "$w/verify"
bash "$here/run_scenarios.sh" "$w/verify" --native verify --det-timing > "$w/verify.txt" 2>&1 || true
if grep -q "MISMATCH" "$w/verify.txt" || grep -q "FAILED" "$w/verify.txt"; then
  grep "MISMATCH\|FAILED" "$w/verify.txt" | head -20; echo "FAIL: verify mismatches"; exit 1
fi
echo "   no mismatches"
echo "== interpreter vs recompiled"
if [ "$engine" != "running recompiled code" ]; then
  echo "   skipped (build has no recompiled code; step 1 used the interpreter)"
elif [ -n "${FA18_SKIP_INTERP:-}" ]; then
  echo "   skipped (FA18_SKIP_INTERP)"
else
  rm -rf "$w/cmp/interp"
  bash "$here/run_scenarios.sh" "$w/cmp/interp" --native off --interpret --det-timing --ram-hash 50 > /dev/null
  if ! bash "$here/compare_runs.sh" "$w/cmp/off" "$w/cmp/interp" > "$w/interp.txt"; then
    grep DIFF "$w/interp.txt"; echo "FAIL: interpreter and recompiled code differ"; exit 1
  fi
  echo "   $(grep -c same "$w/interp.txt") scenarios identical"
fi
echo "PASS"
