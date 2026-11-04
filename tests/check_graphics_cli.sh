#!/usr/bin/env bash
# Verify startup renderer/presenter selection and rejected ambiguous flags.
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo 'usage: tests/check_graphics_cli.sh ORIGINAL.adf [fa18-executable]' >&2
  exit 2
fi

adf=$1
bin=${2:-"$(cd "$(dirname "$0")/.." && pwd)/build/fa18"}
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/*.log; rmdir "$tmp"' EXIT
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
unset FA18_GL_DIRECT

"$bin" --help >"$tmp/help.log" 2>&1
grep -q '^Graphics (the original renderer is the default):$' "$tmp/help.log"
grep -q '^  --gl-direct      present the GL scene/HUD with the GPU' "$tmp/help.log"
grep -q '^Diagnostics and scripted replays:$' "$tmp/help.log"

check_startup() {
  local name=$1 expected=$2
  shift 2
  "$bin" --native off --interpret --fast --frames 2 "$@" "$adf" \
    >"$tmp/$name.log" 2>&1
  if ! grep -q "^graphics: $expected$" "$tmp/$name.log"; then
    echo "FAIL: unexpected graphics startup for $name" >&2
    grep -E '^graphics:|^gl:' "$tmp/$name.log" >&2 || true
    exit 1
  fi
}

check_startup original 'original 320x200, SDL output'
check_startup software 'software 3D 4x, SDL output' --hires 4
check_startup gl 'OpenGL 3D 4x, SDL/CPU output' --gl 4
check_startup direct_default 'OpenGL 3D 3x, direct GPU output' --gl-direct
check_startup direct_scale 'OpenGL 3D 4x, direct GPU output' --gl-direct --gl 4
FA18_GL_DIRECT=0 check_startup direct_env_off 'OpenGL 3D 4x, SDL/CPU output' --gl 4

reject() {
  local name=$1 expected=$2
  shift 2
  if "$bin" "$@" >"$tmp/$name.log" 2>&1; then
    echo "FAIL: $name accepted conflicting/unknown graphics options" >&2
    exit 1
  fi
  grep -q -- "$expected" "$tmp/$name.log"
}

reject conflict '--hires and --gl' --hires 4 --gl 3
reject direct_software '--gl-direct requires OpenGL' --gl-direct --hires 4
reject direct_zero '--gl-direct requires a positive' --gl-direct --gl 0
reject unknown 'Unknown or incomplete option' --invalid-renderer

echo 'PASS: renderer/presenter startup selection, grouped help and invalid flag diagnostics'
