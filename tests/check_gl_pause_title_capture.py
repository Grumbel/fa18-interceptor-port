#!/usr/bin/env python3
"""Validate source 5x7 glyph captures against original presented pixel counts."""

import re
import sys
from pathlib import Path


def main() -> None:
    log = Path(sys.argv[1]).read_text(errors="replace")
    writes = [
        (int(frame), int(bank, 16), int(pixels))
        for frame, bank, pixels in re.findall(
            r"^Pause font capture frame=(\d+) bank=([0-9a-f]+) "
            r"x=\d+ y=\d+ pixels=(\d+)$", log, re.MULTILINE
        )
    ]
    assert len({bank for _, bank, _ in writes}) == 2, (
        "Pause font must be captured in both original screen buffers"
    )

    states = {
        frame: (writer, painted, bitmap, visible)
        for frame, writer, painted, bitmap, visible in (
            map(int, match)
            for match in re.findall(
                r"^Pause title capture use frame=(\d+) writer=(\d+) "
                r"painted=(\d+) bitmap=(\d+) visible=(\d+)$",
                log, re.MULTILINE,
            )
        )
    }
    for frame in range(3550, 3571):
        assert frame in states, f"missing visible-frame observation at {frame}"
        writer, painted, bitmap, visible = states[frame]
        assert painted == bitmap, (
            f"frame {frame}: original glyph writes {painted} != pixel gate {bitmap}"
        )
        assert visible == (bitmap > 24), f"frame {frame}: premature/stale title"
        if frame >= 3552:
            assert writer, f"frame {frame}: still using bitmap gate"
    for frame, expected in ((3552, 5), (3553, 23), (3554, 64), (3558, 133)):
        assert states[frame][1] == expected, (frame, states[frame])
    print(
        f"PASS: {len(writes)} completed 5x7 glyphs in two buffers; "
        "21 displayed frames match original ink counts and visibility"
    )


if __name__ == "__main__":
    main()
