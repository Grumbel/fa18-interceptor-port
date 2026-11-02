# Using retro-foundry's routine notes in Ghidra

This is optional, independent of the game runtime. The script imports the
reference project's **analysis labels**, not its reconstructed machine code.

1. Clone https://github.com/retro-foundry/fa18interceptor alongside this repo.
2. Generate the address index (159 mapped routine notes as of `a8fc6779`):

   ```sh
   python3 tools/reference_addr.py ../fa18interceptor --index > reference-notes.tsv
   ```

3. Open our **relocated** game image in Ghidra. It must be loaded at base
   `$20000`, as produced by `tools/loadseg.py` (Hunk 0 starts at `$20008`).
   The original Amiga executable is a separate user-provided file, never
   part of this repository.
4. In **Script Manager**, add this repository's `tools/ghidra` to Script
   Directories. Run `ImportRetroFoundryNotes.java`, and choose the TSV above.

For headless Ghidra, provide the TSV as the first `-postScript` argument:

```sh
analyzeHeadless PROJECT_DIR PROJECT_NAME -process PROGRAM_NAME \
  -scriptPath tools/ghidra \
  -postScript ImportRetroFoundryNotes.java /absolute/path/reference-notes.tsv
```

The script checks that `$20008` exists and skips addresses not mapped into
Ghidra memory or not marked `verified` in the independent relocation map.
Each mapped routine gets a non-primary `rf_cxxxxx_description` label and a
repeatable comment containing the original live address, Hunk offset and
source note path. Existing function names and user comments remain intact.
Re-running the script is idempotent for an unchanged index.

**Do not treat these as confirmed semantics for our executable.** The other
project used a different disk variant and its notes contain some misleading
physical raw-key names; see `docs/hunk5_input.md`. Even a verified relocation
only proves the reference address-to-Hunk mapping. Compare instruction bytes
and test game behavior before using the names for native implementations.
