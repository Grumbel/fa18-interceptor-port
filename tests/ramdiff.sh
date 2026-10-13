#!/usr/bin/env bash
# Dump game memory at FRAME with --native off and on (deterministic timing)
# for a scenario and list differing addresses above the stack area.
#   tests/ramdiff.sh SCENARIO FRAME WORKDIR [extra options]
set -e
sc=$1; fr=$2; w=$3; shift 3
here=$(cd "$(dirname "$0")/.." && pwd)
for m in off on; do
  rm -rf "$w/rd_$m"; mkdir -p "$w/rd_$m"
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$here/build/fa18" --fast --det-timing --native $m \
    --save "$w/rd_$m" --script "$here/tests/scenarios/$sc.txt" --frames $((fr + 1)) \
    --ram-dump "$fr" "$w/ram_$m.bin" "$@" "$here/assets" > /dev/null 2>&1
done
python3 - "$w/ram_off.bin" "$w/ram_on.bin" <<'PY'
import sys
a=open(sys.argv[1],'rb').read(); b=open(sys.argv[2],'rb').read()
d=[i for i in range(0x20000,min(len(a),len(b))) if a[i]!=b[i]]
print(len(d),'bytes differ')
runs=[]
for i in d:
    if runs and i-runs[-1][1]<=4: runs[-1][1]=i
    else: runs.append([i,i])
for s,e in runs[:30]:
    print('%06x-%06x off=%s on=%s'%(s,e,a[s:e+1][:16].hex(),b[s:e+1][:16].hex()))
PY
