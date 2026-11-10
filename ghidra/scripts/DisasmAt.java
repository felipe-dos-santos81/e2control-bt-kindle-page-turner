// Disassemble one instruction at each address in a TSV (addr<TAB>len<TAB>...), no flow.
// Writes addr<TAB>len<TAB>text, len 0 = undecodable. Used to score q32s against rom.lst.
// @category JieLi
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.Instruction;
import java.io.*;
import java.nio.file.*;

public class DisasmAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        try (PrintWriter out = new PrintWriter(a[1])) {
            for (String line : Files.readAllLines(Path.of(a[0]))) {
                String[] f = line.split("\t");
                Address at = toAddr(Long.decode(f[0]));
                int n = Integer.parseInt(f[1]);
                new DisassembleCommand(at, new AddressSet(at, at.add(n - 1)), false).applyTo(currentProgram, monitor);
                Instruction i = getInstructionAt(at);
                out.println(f[0] + "\t" + (i == null ? 0 : i.getLength()) + "\t" + (i == null ? "" : i.toString()));
            }
        }
    }
}
