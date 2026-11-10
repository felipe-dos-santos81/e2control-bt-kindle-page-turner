// Prepare an imported E2 Control app.bin (q32s, base 0x1E00120):
//   + "rom" block 0x100000 from rom.bin
//   + RAM 0x0-0xFFFF: blocks initialized from flash per ram_copies.tsv (ram<TAB>flash<TAB>size, the
//     startup .data / RAM-code copies found by tools/jlimg.py), uninitialized elsewhere
//   + labels from rom_syms.tsv (addr<TAB>name) and maskrom_stubs.ld (name = ABSOLUTE(addr);)
//   + entry point at the image start
// Args: rom.bin rom_syms.tsv maskrom_stubs.ld ram_copies.tsv
// @category JieLi
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.SourceType;
import java.io.FileInputStream;
import java.nio.file.*;
import java.util.regex.*;

public class SetupE2 extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        Memory mem = currentProgram.getMemory();
        Address code = currentProgram.getMinAddress();
        if (mem.getBlock(toAddr(0x100000)) == null) {
            Path rom = Path.of(a[0]);
            try (FileInputStream in = new FileInputStream(rom.toFile())) {
                MemoryBlock b = mem.createInitializedBlock("rom", toAddr(0x100000), in, Files.size(rom), monitor, false);
                b.setPermissions(true, false, true);
            }
            long at = 0;
            for (String line : Files.readAllLines(Path.of(a[3]))) {
                String[] f = line.split("\t");
                long dst = Long.decode(f[0]), src = Long.decode(f[1]), size = Long.decode(f[2]);
                if (dst > at) ram("ram_" + Long.toHexString(at), at, dst - at, null);
                ram("ram_" + Long.toHexString(dst) + "_from_" + Long.toHexString(src), dst, size, getBytes(toAddr(src), (int) size));
                at = dst + size;
            }
            if (at < 0x10000) ram("ram_" + Long.toHexString(at), at, 0x10000 - at, null);
        }
        int n = 0;
        for (String line : Files.readAllLines(Path.of(a[1]))) {
            String[] f = line.split("\t");
            createLabel(toAddr(Long.decode(f[0])), f[1], true, SourceType.IMPORTED);
            n++;
        }
        Matcher m = Pattern.compile("(\\w+)\\s*=\\s*ABSOLUTE\\((0x[0-9a-fA-F]+)\\)").matcher(Files.readString(Path.of(a[2])));
        while (m.find()) {
            createLabel(toAddr(Long.decode(m.group(2))), m.group(1), false, SourceType.IMPORTED);
            n++;
        }
        createLabel(code, "app_entry", true, SourceType.IMPORTED);
        addEntryPoint(code);
        disassemble(code);
        createFunction(code, "app_entry");
        println("SetupE2: " + n + " labels, entry " + code);
    }

    private void ram(String name, long start, long size, byte[] init) throws Exception {
        Memory mem = currentProgram.getMemory();
        MemoryBlock b = init == null
            ? mem.createUninitializedBlock(name, toAddr(start), size, false)
            : mem.createInitializedBlock(name, toAddr(start), new java.io.ByteArrayInputStream(init), size, monitor, false);
        b.setPermissions(true, true, init != null);
    }
}
