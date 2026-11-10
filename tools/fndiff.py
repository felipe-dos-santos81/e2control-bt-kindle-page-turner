#!/usr/bin/env python3
"""Compare variants by function: hash each function's decompiled C with addresses and
Ghidra auto-names removed, then report functions that are not shared by all variants.
usage: fndiff.py extracted/A extracted/B ...    (reads <dir>/ghidra/decompiled.c)
"""
import collections, hashlib, re, sys
from pathlib import Path

# any Ghidra auto-name carrying an address (FUN_, thunk_FUN_, DAT_, s_text_ADDR, PTR_LAB_...) and raw flash addresses
AUTO = re.compile(r"\b\w*?_(?:0x)?0*1e[0-9a-f]{5}\w*|\b\w*?(?:FUN|DAT|LAB|SUB|UNK)_[0-9a-f]{8}\w*|\b(?:0x)?0*1e[0-9a-f]{5}\b")

def functions(path):
    """yield (address, normalized body, raw body) for each '// <addr>' block in decompiled.c"""
    for blk in re.split(r"^// (?=[0-9a-f]{8}\b)", Path(path).read_text(errors="replace"), flags=re.M)[1:]:
        addr, body = blk.split("\n", 1)[0].split()[0], blk.split("\n", 1)[1]
        yield addr, re.sub(r"\s+", " ", AUTO.sub("@", body)).strip(), body

def main(dirs):
    names = [Path(d).name for d in dirs]
    by = {n: {} for n in names}          # variant -> hash -> [addr]
    raw = {}
    for n, d in zip(names, dirs):
        for a, norm, body in functions(Path(d) / "ghidra" / "decompiled.c"):
            if not a.startswith("01e"):
                continue                  # ROM/RAM are shared by construction; compare flash only
            h = hashlib.sha1(norm.encode()).hexdigest()[:10]
            by[n].setdefault(h, []).append(a); raw[(n, a)] = body
    common = set.intersection(*(set(v) for v in by.values()))
    print(f"{'variant':28} funcs  shared  unique-to-variant  differs-from-some")
    for n in names:
        mine = set(by[n])
        only = {h for h in mine if all(h not in by[m] for m in names if m != n)}
        print(f"{n:28} {sum(map(len, by[n].values())):5}  {len(mine & common):6}  {len(only):17}  {len(mine - common):17}")
    # group non-shared hashes by which variants have them
    groups = collections.defaultdict(list)
    for h in set().union(*by.values()) - common:
        key = tuple(n for n in names if h in by[n])
        groups[key].append(h)
    print("\nnon-shared functions grouped by the variants that contain them:")
    for key, hs in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        ex = by[key[0]][hs[0]][0]
        print(f"  {len(hs):4}  {'+'.join(key)}   e.g. {key[0]}:{ex}")

if __name__ == "__main__":
    main(sys.argv[1:] or sys.exit(__doc__))
