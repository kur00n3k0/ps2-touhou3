// Decompiles every function in the current program to <outdir>/<program>.c
// and writes per-function quality metrics to <outdir>/<program>.metrics.tsv.
// usage (headless): -postScript DecompileAll.java <outdir>
//@category TH03

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

import java.io.File;
import java.io.PrintWriter;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class DecompileAll extends GhidraScript {
    // Markers that mean the decompiler could not express the code as clean C.
    private static final String[] MARKERS = {
        "halt_baddata", "WARNING", "unaff_", "extraout_", "in_", "CONCAT", "SUB", "ZEXT",
        "swi(", "in(", "out(", "segment(", "(code *)", "undefined",
    };

    @Override
    public void run() throws Exception {
        String outDir = getScriptArgs().length > 0 ? getScriptArgs()[0] : ".";
        String name = currentProgram.getName();
        DecompInterface ifc = new DecompInterface();
        ifc.setOptions(new DecompileOptions());
        ifc.openProgram(currentProgram);

        int total = 0, failed = 0, clean = 0;
        try (PrintWriter c = new PrintWriter(new File(outDir, name + ".c"), "UTF-8");
             PrintWriter m = new PrintWriter(new File(outDir, name + ".metrics.tsv"), "UTF-8")) {
            m.print("addr\tname\tlines");
            for (String k : MARKERS) m.print("\t" + k);
            m.println("\tok");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (monitor.isCancelled()) break;
                total++;
                DecompileResults r = ifc.decompileFunction(f, 120, monitor);
                String code = (r != null && r.decompileCompleted() && r.getDecompiledFunction() != null)
                    ? r.getDecompiledFunction().getC() : null;
                if (code == null) {
                    failed++;
                    c.println("/* " + f.getEntryPoint() + " " + f.getName() + ": decompile FAILED: "
                        + (r == null ? "null" : r.getErrorMessage()) + " */\n");
                    m.println(f.getEntryPoint() + "\t" + f.getName() + "\t0\tFAILED");
                    continue;
                }
                c.println("/* ---- " + f.getEntryPoint() + " " + f.getName() + " ---- */");
                c.println(code);
                int bad = 0;
                m.print(f.getEntryPoint() + "\t" + f.getName() + "\t" + code.split("\n").length);
                for (String k : MARKERS) {
                    int n = count(code, k);
                    if (!k.equals("undefined")) bad += n;
                    m.print("\t" + n);
                }
                boolean ok = bad == 0;
                if (ok) clean++;
                m.println("\t" + (ok ? 1 : 0));
            }
        }
        ifc.dispose();
        println(String.format("TH03SUMMARY %s functions=%d failed=%d clean=%d", name, total, failed, clean));
    }

    private static int count(String s, String k) {
        Matcher mt = Pattern.compile(Pattern.quote(k)).matcher(s);
        int n = 0;
        while (mt.find()) n++;
        return n;
    }
}
