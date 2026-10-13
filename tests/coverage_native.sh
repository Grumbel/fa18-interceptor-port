#!/usr/bin/env bash
# Which lines of src/native/*.cpp do the scenarios execute?
#   nix develop -c tests/coverage_native.sh [WORKDIR]       (needs gcov)
# Builds build-cov with --coverage on the native code, runs all scenarios
# (libgcov merges the counters of the runs) and writes
# WORKDIR/<file>.gcov plus a per-file summary; unexecuted lines are
# marked "#####" in the .gcov files.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
w=${1:-$(mktemp -d)}
mkdir -p "$w"
cmake -S "$here" -B "$here/build-cov" -DFA18_COVERAGE=ON -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$here/build-cov" -j"$(nproc)" > "$w/build.txt" 2>&1
find "$here/build-cov" -name '*.gcda' -delete
ls "$here"/tests/scenarios/*.txt | xargs -P "$(nproc)" -I{} sh -c '
  n=$(basename {} .txt); mkdir -p "'"$w"'/s_$n"
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 900 "'"$here"'/build-cov/fa18" --fast \
    --det-timing --save "'"$w"'/s_$n" --script {} "'"$here"'/assets" > /dev/null 2>&1 || true'
objdir=$(dirname "$(find "$here/build-cov" -name 'flow.cpp.gcda' | head -1)")
cd "$w"
for f in "$objdir"/*.gcda; do
  gcov -o "$objdir" "$f" 2>/dev/null | grep -A1 "File '$here/src/native" | grep -v '^--'
done | paste - - | sed "s|File '$here/||; s|' *Lines executed:| |" | column -t
