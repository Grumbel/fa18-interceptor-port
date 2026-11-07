#!/usr/bin/env python3
"""Source-space region and scaled screenshot fixtures; no SDL/ADF needed."""

import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))

from scanout_diff import difference_cells, heatmap, read_ppm, region_counts


def main() -> None:
    scale = 2
    w, h = 320 * scale + 120, 200 * scale
    a = bytearray(w * h * 3)
    b = bytearray(a)
    left = 60
    # One output-pixel difference at the edge of a 2x source cell must
    # mark that cell even though its centre sample is identical.
    def flip(x: int, y: int) -> None:
        off = ((y * scale + 1) * w + left + x * scale) * 3
        b[off] = 255

    flip(0, 0)
    flip(35, 150)
    flip(200, 180)
    flip(290, 180)
    changed = difference_cells(w, h, bytes(a), bytes(b))
    counts = {name: (count, total) for name, count, total in region_counts(changed)}
    assert counts["whole-centre"] == (4, 320 * 200)
    assert counts["upper-screen"] == (1, 320 * 133)
    assert counts["front-cockpit"] == (0, 320 * 12)
    assert counts["lower-panel"] == (3, 320 * 55)
    assert counts["left-attitude"][0] == 1
    assert counts["globe"][0] == 1
    assert counts["right-status"][0] == 1
    preview = heatmap(changed)
    assert len(preview) == 320 * 200 * 3
    assert preview[:3] == b"\xff\x40\x40"
    assert preview[3:6] == b"\x20\x20\x20"
    # Widescreen side differences must not contaminate central coverage.
    b[0] = 42
    assert difference_cells(w, h, bytes(a), bytes(b)) == changed
    try:
        difference_cells(w, h + 1, bytes(a), bytes(b))
        assert False, "invalid height accepted"
    except ValueError:
        pass

    # Exercise the public PPM CLI and its generated heatmap, not just the
    # internal masks. Uses self-contained pictures, not copyrighted assets.
    with tempfile.TemporaryDirectory() as folder:
        root = pathlib.Path(folder)
        normal, hidden, output = (root / name for name in
                                  ("normal.ppm", "hidden.ppm", "diff.ppm"))
        header = f"P6\n{w} {h}\n255\n".encode("ascii")
        normal.write_bytes(header + a)
        hidden.write_bytes(header + b)
        result = subprocess.run([sys.executable, "-B",
                                 str(pathlib.Path(__file__).resolve().parents[1] /
                                     "tools/scanout_diff.py"),
                                 str(normal), str(hidden), "--diff", str(output)],
                                check=True, capture_output=True, text=True)
        assert "whole-centre" in result.stdout
        assert read_ppm(output) == (320, 200, preview)
    print("PASS: bitplane screenshot difference coverage and bounds")


if __name__ == "__main__":
    main()
