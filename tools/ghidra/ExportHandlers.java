// Ghidra headless post-script: define and decompile the object-type handlers of
// PITFALL.EXE.  The dispatcher at 0x405170 jumps through a 256-entry pointer
// table at 0x462350 (indexed by the object's type byte) into code Ghidra's
// auto-analysis never reached.  This script creates functions at the loop
// continuation points, at every handler and at every helper the handlers
// call, then decompiles the handlers one by one, flushing after each so the
// output file grows while the run is in progress.
// Usage: -postScript ExportHandlers.java <output.c>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class ExportHandlers extends GhidraScript {
    private static final int TIMEOUT_SECONDS = 60;

    private DecompInterface newDecompiler() {
        DecompInterface ifc = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        ifc.setOptions(opts);
        ifc.toggleCCode(true);
        ifc.setSimplificationStyle("decompile");
        ifc.openProgram(currentProgram);
        return ifc;
    }

    private boolean ensureFunction(Address a, String name) {
        Listing l = currentProgram.getListing();
        if (l.getInstructionAt(a) == null) {
            new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
        }
        if (l.getFunctionAt(a) == null) {
            new CreateFunctionCmd(a).applyTo(currentProgram, monitor);
        }
        Function f = l.getFunctionAt(a);
        if (f != null && name != null) {
            try { f.setName(name, SourceType.USER_DEFINED); } catch (Exception e) {}
        }
        return f != null;
    }

    // Every CALL target reachable from the handler range becomes a function, so
    // the decompiler sees calls instead of swallowing the callee's code.
    private int defineCallTargets(Address from, Address to) {
        int made = 0;
        Listing l = currentProgram.getListing();
        InstructionIterator it = l.getInstructions(new AddressSet(from, to), true);
        Set<Address> targets = new TreeSet<>();
        while (it.hasNext()) {
            Instruction ins = it.next();
            if (!ins.getMnemonicString().equalsIgnoreCase("CALL")) continue;
            for (Reference r : ins.getReferencesFrom()) {
                if (r.getReferenceType().isCall() && r.getToAddress().getOffset() >= 0x401000 && r.getToAddress().getOffset() < 0x438000)
                    targets.add(r.getToAddress());
            }
        }
        for (Address t : targets) {
            if (l.getFunctionAt(t) == null && ensureFunction(t, null)) made++;
        }
        return made;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String out = args.length > 0 ? args[0] : "handlers_out.c";
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Memory mem = currentProgram.getMemory();

        // Loop continuation points become functions so that a handler's final
        // "jmp 0x405313" is seen as a tail call and ends its body.
        ensureFunction(sp.getAddress(0x405170), "ObjUpdateAll");
        ensureFunction(sp.getAddress(0x4051e5), "ObjUpdateLoop");
        ensureFunction(sp.getAddress(0x405313), "ObjUpdateNext");
        ensureFunction(sp.getAddress(0x4052ee), "ObjUpdateDone");
        // Shared handler tails and helpers (see docs/ENGINE_MAP.md).
        long[] helpers = {0x424d6a, 0x424fbb, 0x424ec4, 0x425051, 0x424f40, 0x424171, 0x4081fc, 0x407aff, 0x407b0f,
                          0x4301ce, 0x412629, 0x433914, 0x433725, 0x433706, 0x433747, 0x424cd4, 0x40855e, 0x4057b0,
                          0x4057c9, 0x42517c, 0x419595, 0x40762b, 0x40b323, 0x417688, 0x420530, 0x419cea, 0x4285d7};
        String[] helperNames = {"ObjExitFar", "ObjExitWide", "ObjIsFar", "ObjIsWide", "ObjIsOffScreen", "ObjWeaponHits", "ObjKilled",
                          "ObjHarryContact", "ObjHarryContactNow", "ObjPriorityTrigger", "ObjFreezeSparkle", "ObjSpawnEffect",
                          "ObjSparkleBurstA", "ObjSparkleBurstB", "ObjSparkleBurst", "ObjHarryOnBox", "ObjDeathEffectKind",
                          "ObjBobCounter", "ItemPickupEffect", "ObjIsVeryFar", "SnakeBiteRange", "CondorWallCheck",
                          "MonkeyChatter", "TreeSnakeContact", "TreeSnakeBiteRange", "SpiderFaceByVelocity", "HarryGrabbedByBat"};
        for (int i = 0; i < helpers.length; i++) ensureFunction(sp.getAddress(helpers[i]), helperNames[i]);

        TreeMap<Long, List<Integer>> handlers = new TreeMap<>();
        for (int t = 0; t < 256; t++) {
            long h = mem.getInt(sp.getAddress(0x462350 + t * 4)) & 0xffffffffL;
            if (h < 0x401000 || h >= 0x44f400) continue;
            handlers.computeIfAbsent(h, k -> new ArrayList<>()).add(t);
        }
        for (Map.Entry<Long, List<Integer>> e : handlers.entrySet()) {
            StringBuilder n = new StringBuilder("ObjType");
            for (int t : e.getValue()) n.append("_").append(t);
            ensureFunction(sp.getAddress(e.getKey()), n.toString());
        }
        // Calls made from the handler region become functions too (two passes:
        // the first pass may disassemble new code with further calls).
        for (int pass = 0; pass < 3; pass++) {
            int made = defineCallTargets(sp.getAddress(0x405300), sp.getAddress(0x437fff));
            println("ExportHandlers: pass " + pass + " defined " + made + " call targets");
            if (made == 0) break;
        }
        // Re-create the handler bodies now that the tails and callees are functions.
        for (long h : handlers.keySet()) {
            Address a = sp.getAddress(h);
            Function f = currentProgram.getListing().getFunctionAt(a);
            if (f != null) new CreateFunctionCmd(f.getName(), a, null, SourceType.USER_DEFINED, true, true).applyTo(currentProgram, monitor);
        }

        DecompInterface ifc = newDecompiler();
        try (PrintWriter w = new PrintWriter(new FileWriter(out), true)) {
            w.println("// Object type handlers of PITFALL.EXE (dispatch table 0x462350), decompiled by ExportHandlers.java");
            List<Address> todo = new ArrayList<>();
            for (long h : handlers.keySet()) todo.add(sp.getAddress(h));
            for (long h : helpers) todo.add(sp.getAddress(h));
            int done = 0;
            for (Address a : todo) {
                Function f = currentProgram.getListing().getFunctionAt(a);
                if (f == null) { w.println("\n// ===== no function at " + a + " ====="); continue; }
                println("ExportHandlers: " + (++done) + "/" + todo.size() + " " + f.getName() + " @ " + a +
                        " (" + f.getBody().getNumAddresses() + " bytes)");
                w.println();
                w.println("// ===== " + f.getName() + " @ " + a + " size=" + f.getBody().getNumAddresses() +
                          " types=" + (handlers.containsKey(a.getOffset()) ? handlers.get(a.getOffset()) : "-") + " =====");
                DecompileResults res = ifc.decompileFunction(f, TIMEOUT_SECONDS, monitor);
                if (res != null && res.decompileCompleted() && res.getDecompiledFunction() != null) {
                    w.print(res.getDecompiledFunction().getC());
                } else {
                    w.println("// DECOMPILE FAILED: " + (res == null ? "null" : res.getErrorMessage()));
                    // A timed-out decompiler process stays wedged: start a fresh one.
                    println("ExportHandlers: restarting the decompiler after a failure on " + f.getName());
                    ifc.dispose();
                    ifc = newDecompiler();
                }
                w.flush();
            }
        }
        ifc.dispose();
        println("ExportHandlers: wrote " + out + " (" + handlers.size() + " handlers)");
    }
}
