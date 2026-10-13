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
