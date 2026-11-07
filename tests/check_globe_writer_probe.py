#!/usr/bin/env python3
"""Verify headless 68000 flight globe against the actual pix/inst5 fill mask.

Usage: python3 tests/check_globe_writer_probe.py ORIGINAL.adf REPLAY.log ROLL.log LEVEL.log GROUND.log
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
    if len(sys.argv) != 6:
        sys.exit("usage: check_globe_writer_probe.py ORIGINAL.adf REPLAY.log ROLL.log LEVEL.log GROUND.log")
    disk = Adf(sys.argv[1])
    entries = {p: h for p, h, size in disk.walk() if size is not None}
    width, height, source, _ = indexed_ilbm(disk.read_file(entries["pix/inst5"]))
    asset_pixels = source  # `source` also names a bank pointer in later checks.
    assert (width, height) == (320, 55)
    log = Path(sys.argv[2]).read_text()
    rows = read_maps(log)
    roll_log = Path(sys.argv[3]).read_text()
    roll_rows = read_maps(roll_log)
    level_log = Path(sys.argv[4]).read_text()
    ground_log = Path(sys.argv[5]).read_text()
    ground_rows = read_maps(ground_log)

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

    # An independent runway-turn script changes the heading by more than
    # eighty degrees, but keeps the aircraft on the ground with zero pitch
    # and at most two degrees of bank. Its original globe stays horizontal:
    # the boundary advances by one *quantized* pixel and then returns to
    # normal. This guards against accidentally using heading as bank or
    # interpreting that single-row split change as a full-panel shake.
    ground_poses = {
        4150: (2240, 0, 173),
        4240: (28640, 160, 174),
        4360: (24104, 160, 174),
        4440: (22944, 0, 173),
    }
    for frame, (heading, bank, boundary) in ground_poses.items():
        assert len(ground_rows.get(frame, {})) == 21, frame
        assert re.search(
            rf"^globe-sample: frame={frame} .* a102=0 a104={heading} "
            rf"a106={bank} hrow={boundary} ",
            ground_log, re.MULTILINE), frame
        assert re.search(
            rf"^globe-view-sequence: frame={frame} mode=0 tick=0 "
            rf"forward=0 view=144 flags=0000 state=2080 altitude=1800 ",
            ground_log, re.MULTILINE), frame
        for y in range(163, 184):
            for x in range(198, 221):
                pen = source[(y - 145) * width + x]
                observed = ground_rows[frame][y][x - 198]
                if pen in (17, 21):
                    expected = "B" if y < boundary else "R"
                else:
                    expected = ground_rows[4150][y][x - 198]
                assert observed == expected, (frame, x, y, expected, observed)

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

    circle = image[0x52d30 - base:0x52d84 - base]
    assert len(circle) == 21 * 4 and sum(byte.bit_count() for byte in circle) == 379
    assert circle[:4] == circle[-4:] == bytes.fromhex("0003f800")

    # Original 68000 control flow: JSR $42f62 projects the sea polygon,
    # BNE skips on failure, otherwise LEA $60684/A0 and $60726/A4,
    # MOVE.W (A0)+,(A4)+, five MOVE.L (A0)+,(A4)+, then RTS. The extra
    # copied tuple is not necessarily part of the count-prefixed polygon.
    assert image[0x44b22 - base:0x44b42 - base] == bytes.fromhex(
        "4eb900042f62661a41f90006068449f90006072638d8"
        "28d828d828d828d828d8")

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
        for pc, address, write_width, value in (
            ("0283d2", "05a964", 2, selection),
            ("04417c", "05a9ae", 4, source),
            ("044182", "05a9b2", 4, second),
        ):
            assert re.search(
                rf"^globe-source-write: frame={frame} pc={pc} "
                rf"address={address} width={write_width} value={value}$",
                roll_log, re.MULTILINE), (frame, pc, address, value)

    # The $451ac input mask is selected from executable Hunk 62 rather than
    # the framebuffer: $52d30 begins a 21x32-bit circular clipping table.
    # Compare the original pre-BLTSIZE register state of the level and
    # banked replays. All affected address/height parameters advance in
    # multiples of one original row (40 bytes for a full bitplane, four for
    # the narrow mask). These are measured blits, not a pitch/bank formula.
    def blit_input(trace: str, frame: int, pc: str) -> dict[str, int]:
        match = re.search(
            rf"^globe-blitter-input: frame={frame} pc={pc} "
            rf"con0=([0-9a-f]{{4}}) con1=([0-9a-f]{{4}}) "
            rf"a=([0-9a-f]{{6}}) b=([0-9a-f]{{6}}) "
            rf"c=([0-9a-f]{{6}}) d=([0-9a-f]{{6}}) "
            rf"amod=(-?\d+) bmod=(-?\d+) cmod=(-?\d+) dmod=(-?\d+)$",
            trace, re.MULTILINE)
        assert match, (frame, pc)
        return dict(zip(
            ("con0", "con1", "a", "b", "c", "d", "amod", "bmod", "cmod", "dmod"),
            (int(x, 16) if i < 6 else int(x) for i, x in enumerate(match.groups()))))

    for frame, advance in ((4476, 4), (4480, 5), (4484, 5), (4488, 6)):
        base_blit = blit_input(level_log, frame, "0451ac")
        roll_blit = blit_input(roll_log, frame, "0451ac")
        assert base_blit["con0"] == roll_blit["con0"] == 0x0FEC, frame
        assert base_blit["con1"] == roll_blit["con1"] == 2, frame
        assert (base_blit["amod"], base_blit["bmod"],
                base_blit["cmod"], base_blit["dmod"]) == (36, 36, 0, 36), frame
        assert base_blit["c"] == 0x52d56, frame
        assert roll_blit["c"] == base_blit["c"] + advance * 4, frame
        for channel in ("a", "b", "d"):
            assert roll_blit[channel] == base_blit[channel] + advance * 40, (frame, channel)
        assert base_blit["b"] == base_blit["d"], frame
        assert roll_blit["b"] == roll_blit["d"], frame

        def source_shape(trace: str) -> tuple[int, int, str, str]:
            match = re.search(
                rf"^globe-blitter-sources: frame={frame} pc=0451ac "
                rf"rows=(\d+) words=(\d+) a-hash=([0-9a-f]{{8}}) "
                rf"c-hash=([0-9a-f]{{8}})$", trace, re.MULTILINE)
            assert match, frame
            return int(match[1]), int(match[2]), match[3], match[4]

        base_rows, base_words, base_ahash, base_chash = source_shape(level_log)
        roll_rows_count, roll_words, roll_ahash, roll_chash = source_shape(roll_log)
        assert (base_rows, base_words) == (11, 2), frame
        assert (roll_rows_count, roll_words) == (11 + advance, 2), frame
        assert base_ahash != "00000000" and roll_ahash != "00000000", frame
        assert base_chash != "00000000" and roll_chash != "00000000", frame
        # BLTCON1 bit 1 selects descending mode. Each extra scanline moves
        # the starting pointer upward, but BOTH flight poses finish at the
        # exact same source/destination address. This detects a mistaken
        # ascending-rectangle interpretation of the traced BLTAPTR values.
        for channel, modulo in (("a", 36), ("b", 36), ("c", 0), ("d", 36)):
            row_stride = base_words * 2 + modulo
            assert (base_blit[channel] - base_rows * row_stride ==
                    roll_blit[channel] - roll_rows_count * row_stride), (frame, channel)

    def image_region_hash(trace: str, frame: int) -> str:
        match = re.search(
            rf"^globe-blitter-region: frame={frame} "
            rf"start=073800 end=074000 hash=([0-9a-f]{{8}})$",
            trace, re.MULTILINE)
        assert match, frame
        return match[1]

    # BLTAPTR's underlying image is live RAM, not a fixed executable asset.
    # The same fixed 2 KiB region changes even in level flight, and the
    # differential bank input changes it further at the same game-code frame.
    assert image_region_hash(level_log, 4476) != image_region_hash(level_log, 4480)
    assert image_region_hash(level_log, 4480) != image_region_hash(roll_log, 4480)

    # The image is an intermediate Amiga blitter scratch bitmap, not static
    # ILBM or executable data. During a single globe update the original
    # writes sparse lines ($452b6), combines them ($4500c), samples the
    # result at $451ac, then clears the temporary region ($450fc).
    # Observe actual bus writes rather than inferring them from PC listings.
    entries = re.findall(
        r"^globe-bitmap-write: frame=4476 pc=([0-9a-f]{6}) "
        r"address=([0-9a-f]{6}) width=(\d+) value=([0-9a-f]+)$",
        roll_log, re.MULTILINE)
    assert len(entries) >= 40, len(entries)
    sites = {pc for pc, _, _, _ in entries}
    assert sites == {"0452b6", "04500c", "0450fc"}, sites
    assert {int(width) for _, _, width, _ in entries} == {2}
    positions = [roll_log.index(f"globe-bitmap-write: frame=4476 pc={pc}")
                 for pc in ("0452b6", "04500c", "0450fc")]
    compositing = roll_log.index("globe-blitter-input: frame=4476 pc=0451ac")
    assert positions[0] < positions[1] < compositing < positions[2]
    assert all(value == "0000" for pc, _, _, value in entries if pc == "0450fc")

    # The tuple list consumed by $44da4 has a direct, measured producer:
    # after a $42f62 projection, $44b36..$44b40 copies the already clipped
    # 2D sea polygon from $60684 to $60726. In the banked replay, its two
    # horizon-edge Y coordinates diverge; in the level replay they coincide.
    def globe_poly(trace: str, frame: int, side: str) -> tuple[int, ...]:
        pc, source = {
            "source": ("044b36", "060684"),
            "copy": ("044b42", "060726"),
            "scaled": ("044de4", "060684"),
        }[side]
        match = re.search(
            rf"^globe-poly-{side}: frame={frame} pc={pc} "
            rf"source={source} count=(\d+) words=([0-9a-f,]+)$",
            trace, re.MULTILINE)
        assert match, (frame, side)
        words = tuple(int(part, 16) for part in match[2].split(","))
        assert len(words) == 11 and words[0] == int(match[1]) == 4, frame
        return words

    for frame in (4476, 4480, 4484, 4488):
        for trace in (level_log, roll_log):
            assert globe_poly(trace, frame, "source") == globe_poly(trace, frame, "copy")

            offset = re.search(
                rf"^globe-poly-offsets: frame={frame} x=(-?\d+) y=(-?\d+)$",
                trace, re.MULTILINE)
            assert offset, frame
            dx, dy = int(offset[1]), int(offset[2])
            source_words = globe_poly(trace, frame, "copy")
            scaled_words = globe_poly(trace, frame, "scaled")
            expected_scaled = [4]
            for i in range(1, 9, 2):
                expected_scaled.extend((193 + (source_words[i] * 24 >> 8) + dx,
                                        162 + (source_words[i + 1] * 31 >> 8) + dy))
            assert scaled_words[:9] == tuple(expected_scaled), (frame, scaled_words)

    level_poly = globe_poly(level_log, 4480, "copy")
    roll_poly = globe_poly(roll_log, 4480, "copy")
    assert level_poly[:9] == (4, 0, 88, 319, 88, 319, 0, 0, 0)
    assert roll_poly[:9] == (4, 0, 51, 319, 127, 319, 0, 0, 0)

    def polygon_attitude(trace: str, frame: int) -> tuple[tuple[int, int, int], tuple[int, ...]]:
        match = re.search(
            rf"^globe-poly-attitude: frame={frame} pitch=(-?\d+) "
            rf"heading=(-?\d+) bank=(-?\d+) matrix=([0-9a-f,]+)$",
            trace, re.MULTILINE)
        assert match, frame
        matrix = tuple(int(word, 16) for word in match[4].split(","))
        assert len(matrix) == 9, matrix
        return tuple(map(int, match.groups()[:3])), matrix

    level_angles, level_matrix = polygon_attitude(level_log, 4480)
    roll_angles, roll_matrix = polygon_attitude(roll_log, 4480)
    assert level_angles[2] == 0 and roll_angles[2] > 0
    assert level_matrix != roll_matrix

    # The original transformed quad is associated with the A2 descriptor's
    # plane-0 base, not with whichever bank happens to be displayed later.
    # This is the authoritative bank key for the optional native capture.
    for frame, desc, base in ((4476, "05a966", "0a5a18"),
                              (4480, "05a976", "06a098"),
                              (4484, "05a966", "0a5a18"),
                              (4488, "05a976", "06a098"),
                              (4693, "05a966", "0a5a18")):
        assert re.search(rf"^globe-poly-bank: frame={frame} desc={desc} "
                         rf"base={base} display={base}$", roll_log, re.MULTILINE), frame

    # Compare the *original* game's rendered colour indices with the first
    # native integer edge model. The source atlas is a 358-cell stencil;
    # clip its y coordinates independently for the touchdown panel shift.
    # Word-aligned x=192..223 interpolation matches the steady roll without
    # errors, with at most two edge pixels different on intermediate poses.
    for code_frame, display_frame, shift, max_errors in (
        (4476, 4484, 0, 1),
        (4480, 4488, -1, 0),
        (4484, 4492, 1, 2),
        (4488, 4496, 0, 0),
        (4693, 4700, 0, 0),
    ):
        poly = globe_poly(roll_log, code_frame, "scaled")
        x0, y0, x1, y1 = poly[1:5]
        assert (x0, x1) == (193, 222)
        checked = errors = 0
        for y in range(163, 184):
            for x in range(198, 221):
                source_y = y - shift
                if (not 163 <= source_y <= 183 or
                        asset_pixels[(source_y - 145) * width + x] not in (17, 21)):
                    continue
                expected_blue = (y - y0 - 1) * 31 < (y1 - y0) * (x - 192)
                actual_blue = roll_rows[display_frame][y][x - 198] == "B"
                errors += expected_blue != actual_blue
                checked += 1
        assert checked == (358 if shift == 0 else 351), (display_frame, checked)
        assert errors <= max_errors, (display_frame, errors, max_errors)

    for frame in (4476, 4480, 4484, 4488):
        for trace in (roll_log, level_log):
            reset = blit_input(trace, frame, "044d08")
            assert reset["con0"] == 0x0722 and reset["con1"] == 0, frame
            assert reset["b"] == 0x52d30 and reset["c"] == reset["d"], frame
            assert (reset["amod"], reset["bmod"],
                    reset["cmod"], reset["dmod"]) == (-24, 0, 36, 36), frame

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
    assert "globe-probe: frames=4450" in ground_log
    print("PASS: 68k globe 358-cell mask, live pens 1/5, pitch/roll "
          "boundaries, level runway turn, touchdown shake latency, "
          "original blitter mask and source-rectangle steps, source-stage "
          "isolation, both writer banks, selector and 4-frame call chain")


if __name__ == "__main__":
    main()
