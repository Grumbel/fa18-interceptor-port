#!/usr/bin/env bash
# Optional ADF-backed integration test for the opt-in visible GL compositor.
# FA18_GL_DIRECT_COMPARE intentionally reads the displayed frame *only* for
# parity diagnostics; normal interactive direct presentation does not.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_gl_direct.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
adf=$1
bin=${2:-"$repo/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.log "$tmp"/*.bin; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy

common=(--native off --interpret --det-timing --fast --gl 1 --frames 3650
        --script "$repo/tests/data/direct_flight.txt")
for mode in centred wide rgba4; do
  args=(--render-fps 60)
  flags=()
  if [ "$mode" != centred ]; then args=(--render-fps 120 --widescreen); fi
  if [ "$mode" = rgba4 ]; then flags=(FA18_GL_FORCE_RGBA4=1); fi
  env "${flags[@]}" FA18_GL_DIRECT_COMPARE=1 FA18_GL_DIRECT_DEBUG=1 FA18_GL_PRESENT_DEBUG=1 \
    "$bin" "${common[@]}" "${args[@]}" --gl-direct \
    --ram-dump 3640 "$tmp/$mode.bin" "$adf" >"$tmp/$mode.log" 2>&1
  if ! grep -q 'gl: direct GPU presentation 1' "$tmp/$mode.log"; then
    echo "FAIL: $mode never presented using the GPU" >&2
    exit 1
  fi
  if [ "$(grep -c 'gl: direct parity frame .* different 0/' "$tmp/$mode.log" || true)" -lt 2 ]; then
    echo "FAIL: $mode GPU/CPU pixel-centre parity not established" >&2
    exit 1
  fi
  if grep -Eq 'gl: direct parity frame .* different [1-9][0-9]*/' "$tmp/$mode.log"; then
    echo "FAIL: $mode differs from the CPU compositor" >&2
    grep 'gl: direct parity frame' "$tmp/$mode.log" >&2
    exit 1
  fi
done

# GL presentation is not permitted to advance simulation or alter emulated
# memory. Keep the traditional CPU compositor as the deterministic oracle.
"$bin" "${common[@]}" --render-fps 120 --widescreen \
  --ram-dump 3640 "$tmp/cpu.bin" "$adf" >"$tmp/cpu.log" 2>&1
cmp "$tmp/wide.bin" "$tmp/cpu.bin"

echo 'PASS: direct OpenGL 4:3/widescreen matches CPU pixels and emulated RAM'
