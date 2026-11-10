#!/usr/bin/env python3
"""Score Ghidra's q32s decoder against the vendor ROM listing.
usage: q32s_score.py rom_insns.tsv ghidra_insns.tsv [N]   (prints the N most common failing forms)
A row passes when Ghidra decodes it with the same length as the vendor objdump.
"""
import collections, re, sys

def rows(p):
    return [l.rstrip("\n").split("\t") for l in open(p)]

def shape(asm):  # collapse operands so failures group by instruction form
    asm = re.sub(r"<[^>]*>", "", asm)
    return re.sub(r"\s+", " ", re.sub(r"-?0x[0-9a-f]+|\b\d+\b", "N", re.sub(r"\br\d+\b", "R", asm))).strip()

# odd addresses are data the vendor objdump decoded anyway (q32s code is 2-byte aligned)
want = [r for r in rows(sys.argv[1]) if int(r[0], 16) % 2 == 0]
got = {r[0]: r for r in rows(sys.argv[2])}
fail = collections.Counter()
ok = 0
for a, n, asm in want:
    g = got.get(a)
    if g and g[1] == n:
        ok += 1
    else:
        fail[(shape(asm), "undecoded" if not g or g[1] == "0" else f"len {g[1]}!={n}")] += 1
print(f"length match: {ok}/{len(want)} = {100 * ok / len(want):.1f}%")
for (s, why), c in fail.most_common(int(sys.argv[3]) if len(sys.argv) > 3 else 40):
    print(f"{c:5}  {why:12} {s}")
