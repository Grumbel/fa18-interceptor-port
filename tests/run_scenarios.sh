#!/usr/bin/env bash
# Run all scripted scenarios headless in parallel.
#   tests/run_scenarios.sh OUTDIR [fa18 options...]
# Each scenario writes OUTDIR/<name>.log and a final screenshot
# OUTDIR/<name>.ppm (frame 6900). With --native verify the per-function
# statistics are summarised at the end.
set -e
out=$1; shift
here=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$out"
ls "$here"/tests/scenarios/*.txt | xargs -P "$(nproc)" -I{} sh -c '
  n=$(basename {} .txt)
  rm -rf "'"$out"'/save_$n"; mkdir -p "'"$out"'/save_$n"
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 900 "'"$here"'/build/fa18" --fast \
    --save "'"$out"'/save_$n" --script {} --screenshot 6900 "'"$out"'/$n.ppm" '"$*"' \
    "'"$here"'/assets" > "'"$out"'/$n.log" 2>&1 || echo "FAILED $n"'
grep -h "MISMATCH" "$out"/*.log | sort | uniq -c | sort -rn | head -40 || true
grep -h "^native " "$out"/*.log | awk '{n[$2]=$3; c[$2]+=substr($4,7); v[$2]+=substr($5,10); m[$2]+=substr($6,12)}
  END {for (k in n) printf "%-30s %s calls=%d verified=%d mismatches=%d\n", k, n[k], c[k], v[k], m[k]}' | sort
