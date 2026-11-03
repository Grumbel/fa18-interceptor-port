#!/usr/bin/env bash
# Run all scripted scenarios headless in parallel.
#   tests/run_scenarios.sh OUTDIR [fa18 options...]
# Each scenario writes OUTDIR/<name>.log and a final screenshot
# OUTDIR/<name>.ppm (frame 6900). With --ram-hash it also writes an atomic
# .verified record of the complete RAM-hash stream and screenshot digests.
# With --native verify the per-function statistics are summarised at the end.
set -e
out=$1; shift
here=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$out"
# Data source: FA18_DATA env, else assets/, else a common ADF next to the tree.
data="${FA18_DATA:-}"
if [ -z "$data" ]; then
  if [ -e "$here/assets/F-18 Interceptor" ]; then
    data="$here/assets"
  else
    for cand in \
      "$here/../FA-18 Interceptor (1988)(Electronic Arts).adf" \
      "$here/../attachments/FA-18 Interceptor (1988)(Electronic Arts).adf" \
      "/home/workdir/attachments/FA-18 Interceptor (1988)(Electronic Arts).adf" \
      "$here/FA-18 Interceptor (1988)(Electronic Arts).adf" \
      "$here/fa18.adf"
    do
      if [ -f "$cand" ]; then data="$cand"; break; fi
    done
  fi
fi
if [ -z "$data" ]; then
  echo "no game data: set FA18_DATA to an ADF path or assets directory" >&2
  exit 1
fi
# Some long scripted runs can exit before their scheduled 'quit' while the
# hashes they did produce still match exactly. An incomplete hash log must
# never be mistaken for a native divergence (or an equivalence success).
# Detect incomplete runs, retry at most three times, then fail explicitly.
FA18_RUN_HASH_STEP=0
previous=
for arg in "$@"; do
  if [ "$previous" = --ram-hash ]; then
    FA18_RUN_HASH_STEP=$arg
    break
  fi
  previous=$arg
done
if ! [[ "$FA18_RUN_HASH_STEP" =~ ^[0-9]+$ ]]; then
  echo "invalid --ram-hash interval: $FA18_RUN_HASH_STEP" >&2
  exit 1
fi
export FA18_RUN_HASH_STEP
ls "$here"/tests/scenarios/*.txt | xargs -P "$(nproc)" -I{} sh -c '
  n=$(basename {} .txt)
  scenario={}
  last_quit=$(awk '\''$2 == "quit" {last = $1} END {print last+0}'\'' "$scenario")
  hash_step=${FA18_RUN_HASH_STEP:-0}
  expected_hash=0
  if [ "$hash_step" -gt 0 ]; then
    expected_hash=$((last_quit / hash_step * hash_step))
  fi
  attempt=0
  while :; do
    attempt=$((attempt + 1))
    rm -f "'"$out"'/$n.ppm" "'"$out"'/$n.verified"
    rm -rf "'"$out"'/save_$n"; mkdir -p "'"$out"'/save_$n"
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 900 "'"$here"'/build/fa18" --fast \
      --save "'"$out"'/save_$n" --script "$scenario" --screenshot 6900 "'"$out"'/$n.ppm" '"$*"' \
      "'"$data"'" > "'"$out"'/$n.log" 2>&1
    rc=$?
    observed=0
    if [ "$hash_step" -gt 0 ]; then
      observed=$(awk '\''/^\[ramhash\] frame / {last = $3} END {print last+0}'\'' "'"$out"'/$n.log")
    fi
    if [ -n "${FA18_SCENARIO_TRACE:-}" ]; then
      echo "SCENARIO $n: step=$hash_step target=$expected_hash observed=$observed rc=$rc" >&2
    fi
    if [ "$rc" -eq 0 ] && [ "$observed" -ge "$expected_hash" ] &&
       [ -s "'"$out"'/$n.ppm" ]; then
      if grep -q MISMATCH "'"$out"'/$n.log"; then
        echo "FAILED $n: native per-call mismatch" >&2
        exit 1
      fi
      if [ "$hash_step" -gt 0 ]; then
        # Verify the *complete* output before the parallel run finishes.
        # A transient workspace sync can otherwise leave a .log with a
        # matching prefix but missing its final lines at comparison time.
        ram_digest=$(grep '\''^\[ramhash\]'\'' "'"$out"'/$n.log" | sha256sum)
        pixel_digest=$(sha256sum "'"$out"'/$n.ppm")
        printf "frame=%s ram=%s screenshot=%s\n" "$observed" \
          "${ram_digest%% *}" "${pixel_digest%% *}" \
          > "'"$out"'/$n.verified.tmp"
        mv "'"$out"'/$n.verified.tmp" "'"$out"'/$n.verified"
      fi
      break
    fi
    if [ "$attempt" -ge 4 ]; then
      echo "FAILED $n: exit=$rc last_hash=$observed expected_hash=$expected_hash (4 attempts)" >&2
      exit 1
    fi
    echo "RETRY $n: exit=$rc last_hash=$observed expected_hash=$expected_hash (attempt $attempt)" >&2
  done'
grep -h "MISMATCH" "$out"/*.log | sort | uniq -c | sort -rn | head -40 || true
grep -h "^native " "$out"/*.log | awk '{n[$2]=$3; c[$2]+=substr($4,7); v[$2]+=substr($5,10); m[$2]+=substr($6,12)}
  END {for (k in n) printf "%-30s %s calls=%d verified=%d mismatches=%d\n", k, n[k], c[k], v[k], m[k]}' | sort
