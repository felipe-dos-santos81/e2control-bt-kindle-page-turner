// Iterate auto-analysis to a fixed point. On q32s, much code is reached only through function pointers
// passed as arguments (task/callback registration): Ghidra records them as PARAM/DATA references
// but never disassembles there. Each pass turns such flash targets into functions when Ghidra's
// PseudoDisassembler accepts them as a plausible subroutine, then re-runs analysis.
// @category JieLi
import ghidra.app.script.GhidraScript;
import ghidra.app.util.PseudoDisassembler;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class Reanalyze extends GhidraScript {
    @Override
    public void run() throws Exception {
        Address lo = toAddr(0x1E00120), hi = currentProgram.getMaxAddress();
        PseudoDisassembler pd = new PseudoDisassembler(currentProgram);
        for (int pass = 1; pass <= 10; pass++) {
            analyzeAll(currentProgram);
            Set<Address> todo = new TreeSet<>();
            for (Instruction i : currentProgram.getListing().getInstructions(true)) {
                for (Reference r : i.getReferencesFrom()) {
                    Address t = r.getToAddress();
                    RefType k = r.getReferenceType();
                    if ((k == RefType.PARAM || k.isData()) && t.compareTo(lo) >= 0 && t.compareTo(hi) <= 0
                        && t.getOffset() % 2 == 0 && getInstructionContaining(t) == null && getDataContaining(t) == null
                        && pd.isValidSubroutine(t, true)) {
                        todo.add(t);
                    }
                }
            }
            println("Reanalyze pass " + pass + ": " + currentProgram.getListing().getNumInstructions()
                + " instructions, " + todo.size() + " new pointer targets");
            if (todo.isEmpty()) break;
            for (Address t : todo) {
                disassemble(t);
                if (getFunctionAt(t) == null) createFunction(t, null);
            }
        }
    }
}
