#!/usr/bin/env bash
# Headless original 68000 replay. No SDL/OpenGL/CMake/Nix dependencies.
set -euo pipefail
if [ "$#" -ne 1 ]; then
  echo 'usage: tests/check_globe_writer_probe.sh ORIGINAL.adf' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -f "$tmp"/probe "$tmp"/replay.log "$tmp"/roll.log "$tmp"/level.log "$tmp"/ground.log; rmdir "$tmp"' EXIT
FA18_COCKPIT_BLIT_TRACE=1 FA18_GLOBE_PROBE_BIN="$tmp/probe" \
  bash "$repo/tools/run_globe_writer_probe.sh" "$1" \
    "$repo/tests/data/cockpit_radar_maneuver.txt" 4700 100 > "$tmp/replay.log" 2>&1
# Reuse the compiled no-SDL probe; isolate bank while pitch is zero.
FA18_COCKPIT_BLIT_TRACE=1 FA18_GLOBE_MAP_FRAMES=3500,4480,4484,4488,4492,4496,4500,4700 \
  FA18_GLOBE_WRITER_REGS=4472:4488:20 \
  FA18_GLOBE_SOURCE_WATCH=4472:4488:24 \
  FA18_GLOBE_BITMAP_WATCH=4476:4480:100 \
  FA18_GLOBE_POLY_TRACE=4476:4693:220 \
  FA18_GLOBE_STAGE_TRACE=4472:4500:60 \
  FA18_GLOBE_PANEL_ANCHOR=4484 \
  "$tmp/probe" "$1" \
    "$repo/tests/data/cockpit_globe_roll.txt" 4700 20 > "$tmp/roll.log" 2>&1
# Same flight-entry script without roll input: check whether a panel shift
# observed during a bank change actually depends on attitude.
FA18_GLOBE_MAP_FRAMES=4476,4480,4484,4488,4492,4496 \
  FA18_GLOBE_WRITER_REGS=4476:4492:20 \
  FA18_GLOBE_POLY_TRACE=4476:4488:20 \
  FA18_GLOBE_STAGE_TRACE=4472:4500:60 \
  FA18_GLOBE_PANEL_ANCHOR=4484 \
  "$tmp/probe" "$1" \
    "$repo/tests/data/cockpit_globe_level.txt" 4496 100 > "$tmp/level.log" 2>&1
# Negative attitude control: a runway turn can change heading by >80 degrees
# while retaining almost zero bank and a level artificial horizon. Check
# that the moving fill is not mistakenly driven by heading or camera shake.
FA18_GLOBE_MAP_FRAMES=4150,4240,4360,4440 \
  "$tmp/probe" "$1" \
    "$repo/tests/data/cockpit_globe_ground_turn.txt" 4450 100 > "$tmp/ground.log" 2>&1
python3 -B "$repo/tests/check_globe_writer_probe.py" "$1" \
  "$tmp/replay.log" "$tmp/roll.log" "$tmp/level.log" "$tmp/ground.log"
