// Write <out>/decompiled.c (all functions) and print analysis-quality counts.
// Args: outdir
// @category JieLi
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;
import java.nio.file.*;

public class ExportE2 extends GhidraScript {
    @Override
    public void run() throws Exception {
        Path out = Path.of(getScriptArgs()[0]);
        Files.createDirectories(out);
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        int fns = 0, failed = 0;
        try (PrintWriter w = new PrintWriter(out.resolve("decompiled.c").toFile())) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                fns++;
                DecompileResults r = d.decompileFunction(f, 60, monitor);
                if (r.decompileCompleted()) {
                    w.println("// " + f.getEntryPoint() + "\n" + r.getDecompiledFunction().getC());
                } else {
                    failed++;
                    w.println("// " + f.getEntryPoint() + " " + f.getName() + ": " + r.getErrorMessage());
                }
            }
        }
        int bad = currentProgram.getBookmarkManager().getBookmarkCount("Error");
        long insns = currentProgram.getListing().getNumInstructions();
        println(String.format("ExportE2: %d instructions, %d functions, %d decompile failures, %d error bookmarks", insns, fns, failed, bad));
    }
}
