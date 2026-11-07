#!/usr/bin/env python3
"""Verify headless 68000 flight globe against the actual pix/inst5 fill mask.

Usage: python3 tests/check_globe_writer_probe.py ORIGINAL.adf REPLAY.log ROLL.log LEVEL.log
Run REPLAY.log using tests/check_globe_writer_probe.sh, not a static fixture.
"""

from pathlib import Path
import math
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from adf import Adf
import loadseg
from check_globe_asset import indexed_ilbm


def read_maps(log: str) -> dict[int, dict[int, str]]:
    rows: dict[int, dict[int, str]] = {}
    for frame, y, pixels in re.findall(r"^globe-map: frame=(\d+) y=(\d+) ([.RB]{23})$",
                                        log, re.MULTILINE):
        rows.setdefault(int(frame), {})[int(y)] = pixels
    return rows


def main() -> None:
    if len(sys.argv) != 5:
        sys.exit("usage: check_globe_writer_probe.py ORIGINAL.adf REPLAY.log ROLL.log LEVEL.log")
    disk = Adf(sys.argv[1])
    entries = {p: h for p, h, size in disk.walk() if size is not None}
    width, height, source, _ = indexed_ilbm(disk.read_file(entries["pix/inst5"]))
    assert (width, height) == (320, 55)
    log = Path(sys.argv[2]).read_text()
    rows = read_maps(log)
    roll_log = Path(sys.argv[3]).read_text()
    roll_rows = read_maps(roll_log)
    level_log = Path(sys.argv[4]).read_text()

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

    # The original panel shifts vertically by one screen pixel on two
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

    # The same +/-1 row displacement happens across the *surrounding panel*.
    # Against frame 4484, 910 non-globe pixels in the panel region align
    # almost perfectly after shifting, but hundreds disagree without it.
    for frame, dy, max_errors, min_baseline in (
        (4488, -1, 2, 300),
        (4492, 1, 2, 300),
        (4496, 0, 0, 0),
    ):
        for variant, sample_log in (("roll", roll_log), ("level", level_log)):
            match = re.search(
                rf"^globe-panel-alignment: frame={frame} anchor=4484 "
                rf"shift-y={re.escape(f'{dy:+d}')} errors=(\d+) "
                rf"baseline-errors=(\d+) samples=(\d+)$",
                sample_log, re.MULTILINE)
            assert match, (variant, frame, dy)
            errors, baseline, samples = map(int, match.groups())
            assert errors <= max_errors and baseline >= min_baseline and samples == 910, (variant, frame)
    assert re.search(r"^globe-sample: frame=4488 .*a102=0 .*a106=0 ",
                     level_log, re.MULTILINE), "level replay did not retain zero pitch/bank"

    # The shake is a *touchdown view sequence*, not roll or compressor stall.
    # Before it starts, the aircraft descends from 1404 to 816. On reaching
    # the 750-unit ground floor, status bit $80 appears in player state +2,
    # and original $39b06 runs mode 1's -40,+40,0,0 byte-offset sequence.
    # The high-speed event flag $5abca is zero throughout this capture.
    for frame, mode, tick, forward, view, state, altitude, speed in (
        (4476, 0, 0, 0, 144, "2000", 1404, 1539),
        (4480, 0, 0, 0, 144, "2000", 816, 1155),
        (4484, 1, 1, -40, 143, "2080", 750, 867),
        (4488, 1, 2, 40, 145, "20c0", 750, 840),
        (4492, 1, 3, 0, 144, "2080", 750, 814),
        (4496, 1, 4, 0, 144, "2080", 750, 789),
    ):
        pattern = (rf"^globe-view-sequence: frame={frame} mode={mode} "
                   rf"tick={tick} forward={forward} view={view} flags=0000 "
                   rf"state={state} altitude={altitude} speed={speed}$")
        assert re.search(pattern, level_log, re.MULTILINE), (frame, pattern)

    # Recover the *original binary*'s view-sequence data. Modes 1 and 3
    # animate with the same four signed bytes, modes 2 and 4 with the
    # inverse pattern. $39b06 scales these by 40 to offset panel rows.
    with tempfile.NamedTemporaryFile(prefix="fa18-shake-", suffix=".exe") as f:
        f.write(disk.read_file(entries["F-18 Interceptor"]))
        f.flush()
        _, _, image = loadseg.load(f.name)
    base = loadseg.LOAD_BASE

    def long(address: int) -> int:
        return int.from_bytes(image[address - base:address - base + 4], "big")

    for mode, expected in enumerate(((-1, 1, 0, 0), (1, -1, 0, 0),
                                      (-1, 1, 0, 0), (1, -1, 0, 0)), start=1):
        header = long(0x39ade + 8 * (mode - 1))
        pattern = long(0x39ade + 8 * (mode - 1) + 4)
        assert image[header - base] == 4, mode
        observed = tuple(int.from_bytes(image[pattern - base + i:
                                              pattern - base + i + 1],
                                        "big", signed=True) for i in range(4))
        assert observed == expected, (mode, observed)

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

    # In the original game_frame, $45368 draws the first instrument-panel
    # layer, then $44c42 makes attitude-sensitive changes before $474ac.
    # Compare *raw bitplane words* (four planes, both banks) at each entry:
    # both replays have a large first-stage rewrite, but the later stage
    # changes ~30+ globe bits with roll and at most one with zero bank/pitch.
    # This isolates the dynamic draw path without assuming either helper is
    # the still-unknown attitude/angle producer.
    def stage_bits(trace: str, frame: int, after: str, before: str,
                   active_bank: int) -> int:
        match = re.search(
            rf"^globe-stage: frame={frame} after={after} before={before} "
            rf"bank0=0a5a18 bits0=(\d+) bank1=06a098 bits1=(\d+)$",
            trace, re.MULTILINE)
        assert match, (frame, after, before)
        values = tuple(map(int, match.groups()))
        assert values[1 - active_bank] == 0, (frame, values)
        return values[active_bank]

    for frame, active_bank in ((4480, 1), (4484, 0)):
        for trace in (roll_log, level_log):
            assert stage_bits(trace, frame, "045368", "0455ba", active_bank) >= 200
        assert stage_bits(roll_log, frame, "044c42", "0474ac", active_bank) >= 20
        assert stage_bits(level_log, frame, "044c42", "0474ac", active_bank) <= 2

    # The source-correct four-step landing shake reaches $44c42 *before*
    # those offsets appear in scanout. At 4480 the blitter reads -40; the
    # panel's -1-row pose first appears on the 4488 displayed framebuffer.
    # Similarly +40 at 4484 appears as +1 row at 4492; zero restores it at
    # 4496. This is a per-buffer presentation delay, not globe roll motion.
    for trace in (roll_log, level_log):
        for frame, forward, dest_y in ((4476, "00000000", "0000009e"),
                                       (4480, "ffffffd8", "0000009d"),
                                       (4484, "00000028", "0000009f"),
                                       (4488, "00000000", "0000009e")):
            assert re.search(rf"^globe-writer-regs: frame={frame} pc=044c42 .*"
                             rf"d0=00000070 d1={dest_y} d2=00000002 .*"
                             rf"forward={forward} stack=", trace, re.MULTILINE), (frame, forward)

    assert "globe-probe: frames=4700" in log
    assert "globe-probe: frames=4700" in roll_log
    print("PASS: 68k globe 358-cell mask, live pens 1/5, pitch/roll "
          "boundaries, touchdown shake latency, source-stage isolation, "
          "both writer banks, selector and 4-frame call chain")


if __name__ == "__main__":
    main()
