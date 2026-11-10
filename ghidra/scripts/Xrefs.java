// Print every reference to each given address: from, type, containing function, instruction.
// Args: hex addresses
// @category JieLi
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Reference;

public class Xrefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String s : getScriptArgs()) {
            for (Reference r : getReferencesTo(toAddr(Long.parseLong(s.replaceFirst("^0x", ""), 16)))) {
                println("XREF " + s + " <- " + r.getFromAddress() + " " + r.getReferenceType() + " fn="
                    + getFunctionContaining(r.getFromAddress()) + " [" + getInstructionAt(r.getFromAddress()) + "]");
            }
        }
    }
}
