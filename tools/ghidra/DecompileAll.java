// Ghidra headless script: create functions at the given entry points,
// apply names, and write decompiled pseudo-C for each to one file.
// Args: <functions.txt> <names.txt> <out.c>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class DecompileAll extends GhidraScript {
  @Override
  public void run() throws Exception {
    String[] args = getScriptArgs();
    List<String> entries = Files.readAllLines(Paths.get(args[0]));
    Map<Long, String> names = new HashMap<>();
    for (String l : Files.readAllLines(Paths.get(args[1]))) {
      String[] p = l.trim().split("\\s+");
      if (p.length == 2) names.put(Long.parseLong(p[0], 16), p[1]);
    }
    AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
    for (String e : entries) {
      e = e.trim();
      if (e.isEmpty()) continue;
      Address a = sp.getAddress(Long.parseLong(e, 16));
      disassemble(a);
      if (getFunctionAt(a) == null) createFunction(a, null);
    }
    for (Map.Entry<Long, String> n : names.entrySet()) {
      Address a = sp.getAddress(n.getKey());
      Function f = getFunctionAt(a);
      if (f == null) { disassemble(a); f = createFunction(a, null); }
      if (f != null) f.setName(n.getValue(), SourceType.USER_DEFINED);
    }
    analyzeChanges(currentProgram);
    DecompInterface di = new DecompInterface();
    di.openProgram(currentProgram);
    try (PrintWriter out = new PrintWriter(new FileWriter(args[2]))) {
      FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
      while (it.hasNext()) {
        Function f = it.next();
        DecompileResults r = di.decompileFunction(f, 60, monitor);
        out.println("// ==== " + f.getName() + " @ " + f.getEntryPoint() + " ====");
        if (r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
        else out.println("// decompile failed: " + r.getErrorMessage());
      }
    }
  }
}
