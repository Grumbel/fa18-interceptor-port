#!/usr/bin/env python3
"""Verify headless 68000 flight globe against the actual pix/inst5 fill mask.

Usage: python3 tests/check_globe_writer_probe.py ORIGINAL.adf REPLAY.log ROLL.log
Run REPLAY.log using tests/check_globe_writer_probe.sh, not a static fixture.
"""

from pathlib import Path
import math
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from adf import Adf
from check_globe_asset import indexed_ilbm


def read_maps(log: str) -> dict[int, dict[int, str]]:
    rows: dict[int, dict[int, str]] = {}
    for frame, y, pixels in re.findall(r"^globe-map: frame=(\d+) y=(\d+) ([.RB]{23})$",
                                        log, re.MULTILINE):
        rows.setdefault(int(frame), {})[int(y)] = pixels
    return rows


def main() -> None:
    if len(sys.argv) != 4:
        sys.exit("usage: check_globe_writer_probe.py ORIGINAL.adf REPLAY.log ROLL.log")
    disk = Adf(sys.argv[1])
    entries = {p: h for p, h, size in disk.walk() if size is not None}
    width, height, source, _ = indexed_ilbm(disk.read_file(entries["pix/inst5"]))
    assert (width, height) == (320, 55)
    log = Path(sys.argv[2]).read_text()
    rows = read_maps(log)
    roll_log = Path(sys.argv[3]).read_text()
    roll_rows = read_maps(roll_log)

    # In this deterministic replay the original game draws three horizontal
    # boundaries; no original bitmap or expected screenshot is checked in.
    # These offsets were *measured* from the running 68k's colour indices.
    expected_offsets = {3500: 0, 4500: 9, 4700: -7}
    for frame, offset in expected_offsets.items():
        assert len(rows.get(frame, {})) == 21, f"missing globe rows for frame {frame}"
        selected = re.search(rf"^globe-sample: frame={frame} .* hrow=(-?\d+) .*"
                             rf"rgb1=880000 rgb5=444477", log, re.MULTILINE)
        assert selected, f"missing original-copper globe sample at frame {frame}"
        assert int(selected.group(1)) == 173 + offset

    source_fills = 0
    for y in range(163, 184):
        for x in range(198, 221):
            original_pen = source[(y - 145) * width + x]
            original_cell = rows[3500][y][x - 198]
            if original_pen in (17, 21):
                source_fills += 1
                # Original source index 17/21 loses bit 4 in the live
                # four-plane framebuffer: red=1, blue=5, not 17/21.
                for frame, offset in expected_offsets.items():
                    expected = "B" if y < 173 + offset else "R"
                    observed = rows[frame][y][x - 198]
                    assert observed == expected, (frame, x, y, expected, observed)
            else:
                # Writer never changes ink outside the original colour
                # stencil. It is valid for such cells to use pen 1 or 5 as
                # other artwork, but their displayed pen must be constant.
                for frame in expected_offsets:
                    assert rows[frame][y][x - 198] == original_cell, (frame, x, y)
    assert source_fills == 358, source_fills

    # With only joystick left, the aircraft has zero pitch and its bank
    # field reaches 2216/80 = 27.7 degrees. The original on-screen globe is
    # tilted in the *opposite* direction; a measured -8-degree intermediate
    # and the later -20-degree/+1 px boundary match all 358 cells. These are
    # display poses, not a general conversion from the game's bank value.
    roll_poses = {3500: (0.0, 0.0, 0),
                  4480: (-8.0, 0.65, 1440),
                  4500: (-20.0, 1.0, 2216),
                  4700: (-20.0, 1.0, 2216)}
    for frame, (angle, offset, bank_word) in roll_poses.items():
        assert len(roll_rows.get(frame, {})) == 21
        source_pose = frame == 3500
        assert re.search(
            rf"^globe-sample: frame={frame} .* a102=0 .* a106="
            rf"{bank_word} hrow=",
            roll_log, re.MULTILINE), frame
        sin_bank = math.sin(math.radians(angle))
        cos_bank = math.cos(math.radians(angle))
        for y in range(163, 184):
            for x in range(198, 221):
                source_pen = source[(y - 145) * width + x]
                observed = roll_rows[frame][y][x - 198]
                if source_pen not in (17, 21):
                    assert observed == roll_rows[3500][y][x - 198], (frame, x, y)
                    continue
                if source_pose:
                    expected = "B" if y < 173 else "R"
                else:
                    d = (x - 209) * sin_bank + (y - 173) * cos_bank - offset
                    expected = "B" if d < 0 else "R"
                assert observed == expected, (frame, x, y, expected, observed)

    # The original globe fill itself shifts by one screen pixel on two
    # consecutive redraws. These are not red/blue fills lost behind other
    # ink. Both shifted 351-cell masks match the original 358-cell shape
    # exactly after clipping the circle to the sampled 21-row viewport.
    mask = {(x, y) for y in range(163, 184) for x in range(198, 221)
            if source[(y - 145) * width + x] in (17, 21)}
    for frame, shift_y in ((4488, -1), (4492, 1)):
        assert len(roll_rows.get(frame, {})) == 21, frame
        expected = {(x, y + shift_y) for x, y in mask
                    if 163 <= y + shift_y <= 183}
        observed = {(x, y) for y in range(163, 184) for x in range(198, 221)
                    if roll_rows[frame][y][x - 198] in ("R", "B")}
        assert len(expected) == 351 and observed == expected, frame

    # Exercise the geometry fitting tool as well: it must align the moving
    # stencil instead of interpreting forty displaced cells as occlusion.
    fitting = subprocess.run(
        [sys.executable, str(Path(__file__).resolve().parents[1] /
                           "tools/fit_globe_boundary.py"), sys.argv[1], sys.argv[3]],
        check=True, capture_output=True, text=True).stdout
    for frame, shift_y in ((4488, -1), (4492, 1)):
        assert re.search(rf"^frame={frame} .*shift-y={re.escape(f'{shift_y:+d}')} "
                         rf"missing=0 extra=0 fit-errors=0 ",
                         fitting, re.MULTILINE), (frame, fitting)

    # Active (not initialization-only) original blitter writes are present in
    # both displayed screen banks for this flight. These are low-level sites,
    # not yet identified as the high-level attitude-control routine.
    for pc, minterm in (("044d08", "22"), ("0451ac", "ec")):
        for bank in ("06a098", "0a5a18"):
            pattern = rf"^cockpit-blit: zone=globe pc={pc} bank={bank} plane=2 "
            pattern += rf"area minterm={minterm} .*frames=34\d\d\.\."
            assert re.search(pattern, log, re.MULTILINE), (pc, bank)

    # The 68k flight loop calls $44c42 from $213a8, via the clipping helper
    # $45ce6 into the actual BLTSIZE writes. Consecutive 4-frame redraws
    # alternate the buffer address ($06a098/$0a5a18 in the blitter trace).
    for i, frame in enumerate((4472, 4476, 4480, 4484, 4488)):
        for pc in ("044c42", "045ce6", "044d08", "0451ac"):
            pattern = rf"^globe-writer-regs: frame={frame} pc={pc} .* stack="
            assert re.search(pattern, roll_log, re.MULTILINE), (frame, pc)
        assert re.search(rf"^globe-writer-regs: frame={frame} pc=044d08 .*"
                         rf" stack=000213ae,", roll_log, re.MULTILINE), frame
        assert re.search(rf"^globe-writer-regs: frame={frame} pc=045ce6 .*"
                         rf" stack=00044c86,000213ae,",
                         roll_log, re.MULTILINE), frame
        assert re.search(rf"^globe-writer-regs: frame={frame} pc=0451ac .*"
                         rf" stack=00044df2,00044d10,000213ae,",
                         roll_log, re.MULTILINE), frame
        source = "0005a976" if i % 2 == 0 else "0005a966"
        assert re.search(rf"^globe-writer-regs: frame={frame} pc=044d08 .*"
                         rf" d6=[0-9a-f]{{4}}0542 .* a2={source} ",
                         roll_log, re.MULTILINE), frame
        selection = "0001" if i % 2 == 0 else "0000"
        second = "0005a99a" if i % 2 == 0 else "0005a986"
        for pc, address, width, value in (
            ("0283d2", "05a964", 2, selection),
            ("04417c", "05a9ae", 4, source),
            ("044182", "05a9b2", 4, second),
        ):
            assert re.search(
                rf"^globe-source-write: frame={frame} pc={pc} "
                rf"address={address} width={width} value={value}$",
                roll_log, re.MULTILINE), (frame, pc, address, value)

    for frame, screen in ((4480, "06bb28"), (4500, "0a74a8")):
        assert re.search(rf"^globe-sample: frame={frame} rowkey={screen} ",
                         roll_log, re.MULTILINE), frame

    assert "globe-probe: frames=4700" in log
    assert "globe-probe: frames=4700" in roll_log
    print("PASS: 68k globe 358-cell mask, live pens 1/5, pitch/roll "
          "boundaries, moving stencil, both writer banks, selector "
          "and 4-frame call chain")


if __name__ == "__main__":
    main()
