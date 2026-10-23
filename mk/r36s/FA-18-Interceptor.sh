#!/bin/bash
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PortMaster launcher for R36S / ArkOS.
CONTROL="${PORT_32BIT:-"/opt/inttools/control.txt"}"
source "$CONTROL" 2>/dev/null || true
get_controls 2>/dev/null || true

GAMEDIR="/$directory/ports/fa18"
cd "$GAMEDIR" || exit 1

# Prefer an ADF next to the binary, then common names in the port folder.
ADF=""
for candidate in \
  "FA-18 Interceptor (1988)(Electronic Arts).adf" \
  "FA-18 Interceptor.adf" \
  "fa18.adf" \
  "disk.adf"
do
  if [ -f "$GAMEDIR/$candidate" ]; then
    ADF="$GAMEDIR/$candidate"
    break
  fi
done

export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
if [ -n "$ADF" ]; then
  exec ./fa18 --native on "$ADF"
else
  echo "No ADF found in $GAMEDIR. Place the original disk image there."
  sleep 3
  exit 1
fi
