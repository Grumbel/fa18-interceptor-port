// Ghidra headless script: create functions at the given entry points,
// apply names, and write decompiled pseudo-C for each to one file.
// Args: <functions.txt> <names.txt> <out.c> [seeds.txt: extra code addresses]
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
    if (args.length > 3) {
      // Known instruction addresses (coverage): make sure code after calls
      // that Ghidra guessed to be no-return is disassembled too.
      for (String l : Files.readAllLines(Paths.get(args[3]))) {
        l = l.trim();
        if (l.isEmpty() || l.startsWith("#")) continue;
        Address a = sp.getAddress(Long.parseLong(l.split("\\s+")[0], 16));
        if (getInstructionAt(a) == null) disassemble(a);
      }
    }
    analyzeChanges(currentProgram);
    // Ghidra sometimes infers "no return" (e.g. for the WaitBlit stub) and
    // then drops the rest of the caller; none of the game's routines are
    // no-return except exit().
    for (Function f : currentProgram.getFunctionManager().getFunctions(true)) f.setNoReturn(false);
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
