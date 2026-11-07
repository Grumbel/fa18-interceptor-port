#!/usr/bin/env python3
"""Fit signed-distance red/blue boundaries against original flight pixels.

This is a measurement tool, not the game's original pitch/bank formula:

  FA18_GLOBE_MAP_FRAMES=3500,4440,4480,4500,4700 \
    bash tools/run_globe_writer_probe.sh ORIGINAL.adf \
      tests/data/cockpit_globe_roll.txt 4750 20 > /tmp/roll.log
  python3 tools/fit_globe_boundary.py ORIGINAL.adf /tmp/roll.log

Only the original ADF's 358 coloured fill cells are used. Marks, lettering
and the mechanical bezel never enter the fit. No asset data is checked in.
"""

import math
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))

from check_globe_asset import indexed_ilbm
from adf import Adf


def fit(cells: list[tuple[int, int, bool]]) -> tuple[int, float, float, float, float]:
    """Return errors, best angle/offset, min/max exact-fit angle.

    Angles are measured in native screen coordinates. For each trial
    angle, find the optimal threshold of projected cell centres. The
    discrete 320x200 grid often admits a *range* of exact fit angles;
    report it instead of claiming spurious subpixel precision.
    """
    exact: list[tuple[float, float, float]] = []
    best = (len(cells), 0.0, 0.0)
    for tenth in range(-1800, 1801, 5):
        angle = tenth / 10.0
        s = math.sin(math.radians(angle))
        c = math.cos(math.radians(angle))
        projected = sorted(((dx * s + dy * c, blue) for dx, dy, blue in cells))
        errors = sum(blue for _, blue in projected)  # initial: all red
        lo_errors = errors
        threshold = projected[0][0] - 1
        idx = 0
        while idx < len(projected):
            value = projected[idx][0]
            while idx < len(projected) and abs(projected[idx][0] - value) < 1e-12:
                errors += -1 if projected[idx][1] else 1
                idx += 1
            if errors < lo_errors:
                lo_errors = errors
                threshold = (value + projected[idx][0]) / 2 \
                    if idx < len(projected) else value + 1
        if lo_errors < best[0]:
            best = (lo_errors, angle, threshold)
        if lo_errors == 0:
            margin = min(v for v, blue in projected if not blue) - \
                     max(v for v, blue in projected if blue)
            exact.append((angle, threshold, margin))
    if exact:
        # A large margin is a stable angle in the face of quantised pixel
        # centres. We keep the entire exact-fit range alongside the choice.
        angle, offset, _ = max(exact, key=lambda trial: trial[2])
        return 0, angle, offset, min(a for a, _, _ in exact), max(a for a, _, _ in exact)
    return best[0], best[1], best[2], math.nan, math.nan


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit("usage: fit_globe_boundary.py ORIGINAL.adf PROBE.log")
    disk = Adf(sys.argv[1])
    entries = {p: h for p, h, size in disk.walk() if size is not None}
    width, _, source, _ = indexed_ilbm(disk.read_file(entries["pix/inst5"]))
    log = Path(sys.argv[2]).read_text()
    rows: dict[int, dict[int, str]] = {}
    for frame, y, indices in re.findall(
            r"^globe-map: frame=(\d+) y=(\d+) ([.RB]{23})$", log, re.MULTILINE):
        rows.setdefault(int(frame), {})[int(y)] = indices
    for frame in sorted(rows):
        if len(rows[frame]) != 21:
            raise ValueError(f"frame {frame}: incomplete map")
        cells = []
        for y in range(163, 184):
            for x in range(198, 221):
                if source[(y - 145) * width + x] not in (17, 21):
                    continue
                colour = rows[frame][y][x - 198]
                if colour not in ("R", "B"):
                    raise ValueError(f"frame {frame}: missing fill at {x},{y}")
                cells.append((x - 209, y - 173, colour == "B"))
        red = sum(not blue for _, _, blue in cells)
        blue = len(cells) - red
        if min(red, blue) == 0:
            print(f"frame={frame} fully-clipped red={red} blue={blue}")
            continue
        errors, angle, offset, lo, hi = fit(cells)
        line = re.search(rf"^globe-sample: frame={frame} .*"
                         rf"a102=(\d+) a104=(\d+) a106=(\d+)", log, re.MULTILINE)
        attitude = (f" p={int(line[1])/80:.1f} h={int(line[2])/80:.1f}"
                    f" b={int(line[3])/80:.1f}") if line else ""
        print(f"frame={frame}{attitude} red={red} blue={blue} "
              f"fit-errors={errors} angle={angle:+.1f} offset={offset:+.2f} "
              f"exact-angle-range=[{lo:+.1f},{hi:+.1f}]")


if __name__ == "__main__":
    main()
