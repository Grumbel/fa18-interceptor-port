#!/usr/bin/env python3
"""Compare forced original globe poses against their native stencil."""

from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from adf import Adf
from check_globe_asset import indexed_ilbm

disk = Adf(sys.argv[1])
entries = {path: header for path, header, size in disk.walk() if size is not None}
width, height, pixels, _ = indexed_ilbm(disk.read_file(entries["pix/inst5"]))
assert (width, height) == (320, 55)
log = Path(sys.argv[2]).read_text()
mode = sys.argv[3] if len(sys.argv) > 3 else "inverted"
bank, vertices = {
    "inverted": (14400, "00c1,00ac,00de,00ac,00de,00b7,00c1,00b7"),
    "right": (7200, "00cf,00a2,00cf,00b7,00de,00b7,00de,00a2"),
    "left": (21600, "00cf,00a2,00cf,00b7,00c1,00b7,00c1,00a2"),
}[mode]
assert re.search(rf"globe-poly-attitude: frame=\d+ pitch=0 .* bank={bank} ", log)
assert re.search(r"globe-poly-scaled: frame=\d+ .*count=4 words=0004," + vertices, log)
rows = {int(y): row for y, row in re.findall(
    r"globe-map: frame=4700 y=(\d+) ([.RB]+)", log)}
assert len(rows) == 21
checked = 0
for y in range(163, 184):
    for x in range(198, 221):
        if pixels[(y - 145) * width + x] not in (17, 21):
            continue
        sky = y >= 173 if mode == "inverted" else x >= 207 if mode == "right" else x <= 207
        expected = "B" if sky else "R"
        assert rows[y][x - 198] == expected, (x, y, rows[y][x - 198], expected)
        checked += 1
assert checked == 358
print(f"{mode} globe: {checked} original fill cells match the native edge")
