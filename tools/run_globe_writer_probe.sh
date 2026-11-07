#!/usr/bin/env bash
# No CMake, Nix, SDL or GL dependencies: rebuild a pure-interpreter diagnostic
# from the same original Amiga model used by the graphical front end.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
if [ "$#" -lt 2 ]; then
  echo 'usage: tools/run_globe_writer_probe.sh ORIGINAL.adf SCRIPT [frames] [sample-every]' >&2
  exit 2
fi
if [ -n "${FA18_GLOBE_PROBE_BIN:-}" ]; then
  out=$FA18_GLOBE_PROBE_BIN
else
  out=$(mktemp "${TMPDIR:-/tmp}/fa18-globe-probe.XXXXXX")
  trap 'rm -f "$out"' EXIT
fi
"${CXX:-g++}" -std=c++20 -O2 -I"$repo/src" \
  "$repo/tools/globe_writer_probe.cpp" \
  "$repo/src/amiga/machine.cpp" "$repo/src/amiga/custom.cpp" \
  "$repo/src/amiga/hle.cpp" "$repo/src/amiga/savestate.cpp" \
  "$repo/src/m68k/cpu.cpp" "$repo/src/native/runtime.cpp" \
  -o "$out"
"$out" "$@"
