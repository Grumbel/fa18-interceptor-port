// Import the TSV made by `python3 tools/reference_addr.py REF --index`.
// @category FA18
// @runtime Java
//
// Adds non-primary rf_ labels and repeatable provenance comments. It never
// renames functions, makes new functions, disassembles, or overwrites comments.
// The source Amiga executable may differ: these are RE hints, not verified
// names for the uncracked binary. See docs/retro_foundry_comparison.md.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import java.io.File;
import java.nio.file.Files;
import java.util.List;

public class ImportRetroFoundryNotes extends GhidraScript {
  @Override
  public void run() throws Exception {
    String[] args = getScriptArgs();
    File tsv = args.length > 0 ? new File(args[0]) : askFile("Reference address index", "Open TSV");
    List<String> rows = Files.readAllLines(tsv.toPath());
    int created = 0, existing = 0, skipped = 0, annotated = 0;

    // This project imports tools/loadseg.py's flat image at its real base.
    if (!currentProgram.getMemory().contains(toAddr(0x20008L))) {
      printerr("Expected Hunk-0 payload at $20008. Import the image with base $20000 first.");
      return;
    }

    for (String row : rows) {
      monitor.checkCancelled();
      if (row.startsWith("port\t") || row.trim().isEmpty()) continue;
      String[] fields = row.split("\t", -1);
      if (fields.length != 5 || !"verified".equals(fields[3])) {
        skipped++;
        continue;
      }
      try {
        long offset = Long.parseLong(fields[0].replace("$", ""), 16);
        Address address = toAddr(offset);
        if (!currentProgram.getMemory().contains(address)) {
          skipped++;
          continue;
        }
        String path = fields[4].replace('\\', '/');
        String filename = path.substring(path.lastIndexOf('/') + 1);
        if (!filename.endsWith(".md") || !filename.matches("c[0-9a-fA-F]{5}.*\\.md")) {
          skipped++;
          continue;
        }
        String name = "rf_" + filename.substring(0, filename.length() - 3)
            .replaceAll("[^A-Za-z0-9_]", "_");
        boolean present = false;
        for (Symbol symbol : currentProgram.getSymbolTable().getSymbols(address)) {
          if (name.equals(symbol.getName())) { present = true; break; }
        }
        if (present) {
          existing++;
        } else {
          createLabel(address, name, false, SourceType.USER_DEFINED);
          created++;
        }

        String provenance = "retro-foundry " + fields[2] + " (" + fields[1] + ") " + path;
        String comment = getRepeatableComment(address);
        if (comment == null || !comment.contains(provenance)) {
          setRepeatableComment(address,
              comment == null || comment.isEmpty() ? provenance : comment + "\n" + provenance);
          annotated++;
        }
      } catch (Exception ex) {
        printerr("Skipped reference row: " + row + ": " + ex.getMessage());
        skipped++;
      }
    }
    println("retro-foundry: " + created + " labels created, " + existing
        + " already present, " + annotated + " comments added, " + skipped + " skipped");
  }
}
