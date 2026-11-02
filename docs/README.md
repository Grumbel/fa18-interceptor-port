# Reverse-engineering notes

Most notes in this directory were written before October 2026 against a
corrupt extraction of the executable (truncated at 311832 bytes) and, for
the `$00Cxxxxx` addresses, against FS-UAE RAM dumps (`dumps/`). Their file
offsets are still valid (the prefix of the file was intact), but several
interpretations were guesses; treat them as historical.

Current, verified knowledge lives in `../TODO.md` ("Findings") and in the
code (`src/amiga/hle.cpp`, `src/amiga/custom.cpp`). Runtime addresses in
the new tools (`tools/disasm.py`) use the loader layout: hunks back to back
from $20000, hunk 0 payload at $20008.

See `retro_foundry_comparison.md` for independent 68000 trace/reconstruction
evidence from `retro-foundry/fa18interceptor` and a tool that translates its
runtime addresses to the addresses used in this port.
`retro_foundry_hunk36.md` cross-checks the independently reconstructed line
blitter and four-plane display paths against our rendering hooks, including a
specific negative-mode colour question for the hi-res observer.

`hunk5_input.md` drills down into the input command tables, physical Amiga
raw-key identities, control-record fields, and the tests needed before
registering more native Hunk-5 routines.

The mapped reference routine notes can also be imported as non-primary
labels into Ghidra; instructions are in `../tools/ghidra/README.md`.
