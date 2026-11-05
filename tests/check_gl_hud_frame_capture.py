#!/usr/bin/env python3
"""Check that the flight HUD outline was captured on both real screen banks.

This inspects interpreter hook traces, not pixels. The paired shell test also
checks that the writer-driven HUD compositor is bit-exact against its previous
source-pixel implementation in several gameplay and transition scenarios.
"""

import re
import sys
from pathlib import Path


def main() -> None:
    log = Path(sys.argv[1]).read_text(errors="replace")
    captures = [
        (int(frame), int(bank, 16))
        for frame, bank in re.findall(
            r"^HUD frame captured frame=(\d+) bank=([0-9a-f]+) segments=5$",
            log,
            flags=re.MULTILINE,
        )
    ]
    assert captures, "no complete five-line HUD outline capture"
    banks = {bank for frame, bank in captures if frame <= 4000}
    assert len(banks) >= 2, f"expected both flight screen banks: {banks}"

    uses = [
        (int(frame), int(writer), int(visible), int(rails))
        for frame, writer, visible, rails in re.findall(
            r"^HUD frame capture use frame=(\d+) writer=(\d+) visible=(\d+) rails=(\d+)$",
            log,
            flags=re.MULTILINE,
        )
    ]
    assert (4000, 1, 1, 1) in uses, (
        "writer-driven HUD frame was not selected with crown and rails at "
        f"settled frame 4000: {uses[-10:]}"
    )
    print(
        f"PASS: {len(captures)} complete outline redraws across "
        f"{len(banks)} original screen banks; active at frame 4000"
    )


if __name__ == "__main__":
    main()
