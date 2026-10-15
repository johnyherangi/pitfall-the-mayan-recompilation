// Ghidra headless post-script: dump every function's decompilation + a symbol table.
// Usage (from analyzeHeadless): -postScript ExportDecomp.java <output.c>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import java.io.*;

public class ExportDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String out = args.length > 0 ? args[0] : "decomp_out.c";
        DecompInterface ifc = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        ifc.setOptions(opts);
        ifc.toggleCCode(true);
        ifc.toggleSyntaxTree(false);
        ifc.setSimplificationStyle("decompile");
        ifc.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(new FileWriter(out))) {
            w.println("// Decompiled from " + currentProgram.getName() + " by Ghidra " +
                      ghidra.framework.Application.getApplicationVersion());
            w.println("// image base " + currentProgram.getImageBase());
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            int n = 0;
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                if (f.isExternal()) continue;
                w.println();
                w.println("// ===== " + f.getName() + " @ " + f.getEntryPoint() +
                          " size=" + f.getBody().getNumAddresses() +
                          " callers=" + f.getCallingFunctions(monitor).size() + " =====");
                DecompileResults res = ifc.decompileFunction(f, 120, monitor);
                if (res != null && res.decompileCompleted() && res.getDecompiledFunction() != null) {
                    w.print(res.getDecompiledFunction().getC());
                } else {
                    w.println("// DECOMPILE FAILED: " + (res == null ? "null" : res.getErrorMessage()));
                }
                n++;
            }
            w.println("\n// ===== " + n + " functions =====");
        }
        // Symbol / data table
        try (PrintWriter w = new PrintWriter(new FileWriter(out + ".symbols.txt"))) {
            SymbolIterator si = currentProgram.getSymbolTable().getAllSymbols(true);
            while (si.hasNext()) {
                Symbol s = si.next();
                if (s.isExternal()) continue;
                Data d = currentProgram.getListing().getDefinedDataAt(s.getAddress());
                String t = d != null ? d.getDataType().getName() : "";
                String v = (d != null && d.getValue() != null) ? d.getValue().toString().replace("\n","\\n") : "";
                w.println(s.getAddress() + "\t" + s.getSymbolType() + "\t" + s.getName() + "\t" + t + "\t" + v);
            }
        }
        // Defined data (initialized tables) in .data/.rdata as hex
        try (PrintWriter w = new PrintWriter(new FileWriter(out + ".data.txt"))) {
            DataIterator di = currentProgram.getListing().getDefinedData(true);
            while (di.hasNext()) {
                Data d = di.next();
                if (d.getLength() > 4096) continue;
                byte[] b = d.getBytes();
                StringBuilder sb = new StringBuilder();
                for (byte x : b) sb.append(String.format("%02x", x & 0xff));
                w.println(d.getAddress() + "\t" + d.getDataType().getName() + "\t" + d.getLength() + "\t" + sb);
            }
        }
        ifc.dispose();
        println("ExportDecomp: wrote " + out);
    }
}
