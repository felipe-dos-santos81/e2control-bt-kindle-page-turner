#!/usr/bin/env python3
"""Turn the SDK's BD19 ROM listing (cpu/bd19/tools/rom.lst, vendor q32s objdump) into:
  OUT/rom.bin        ROM bytes at 0x100000 (gaps = 0xFF)
  OUT/rom_syms.tsv   addr<TAB>name        (function labels)
  OUT/rom_insns.tsv  addr<TAB>len<TAB>asm (ground truth for the Ghidra q32s module)
usage: romlst.py rom.lst OUTDIR
"""
import re, sys
from pathlib import Path

BASE = 0x100000
INS = re.compile(r"^ +([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*?)(?:\s+##.*)?$")
DAT = re.compile(r"^ +([0-9a-f]+):\t ((?:[0-9a-f]{2} )+)")
SYM = re.compile(r"^([A-Za-z_.$][\w.$]*):$")

out = Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
mem, syms, insns, label = {}, [], [], None
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    line = line.rstrip("\n")
    if m := SYM.match(line):
        label = m[1]
    elif m := INS.match(line):
        a, b = int(m[1], 16), bytes.fromhex(m[2])
        if label:
            syms.append((a, label)); label = None
        mem.update((a + i, v) for i, v in enumerate(b))
        insns.append((a, len(b), m[3].strip()))
    elif m := DAT.match(line):  # data rows (strings, tables)
        a, b = int(m[1], 16), bytes.fromhex(m[2])
        mem.update((a + i, v) for i, v in enumerate(b))
end = max(mem) + 1
(out / "rom.bin").write_bytes(bytes(mem.get(a, 0xFF) for a in range(BASE, end)))
(out / "rom_syms.tsv").write_text("".join(f"{a:#x}\t{n}\n" for a, n in syms))
(out / "rom_insns.tsv").write_text("".join(f"{a:#x}\t{l}\t{t}\n" for a, l, t in insns))
print(f"{len(insns)} insns, {len(syms)} syms, rom {BASE:#x}-{end:#x}")
