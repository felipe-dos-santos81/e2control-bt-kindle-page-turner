#!/bin/sh
# Import an unpacked app.bin into the Ghidra project ghidra/project/E2, analyze it, export pseudo-C.
# usage: GHIDRA_HOME=... ghidra/import.sh extracted/<variant>
set -eu
: "${GHIDRA_HOME:?set GHIDRA_HOME}"
root=$(cd "$(dirname "$0")/.." && pwd)
dir=$(cd "$1" && pwd); name=$(basename "$dir")
mkdir -p "$root/ghidra/project"
cp "$dir/app.bin" "$dir/$name.bin"   # program name = variant
"$GHIDRA_HOME/support/analyzeHeadless" "$root/ghidra/project" E2 -import "$dir/$name.bin" -overwrite \
  -processor q32s:LE:32:default -loader BinaryLoader -loader-baseAddr 0x1E00120 \
  -scriptPath "$root/ghidra/scripts" \
  -preScript SetupE2.java "$root/extracted/rom/rom.bin" "$root/extracted/rom/rom_syms.tsv" "$root/sdk/maskrom_stubs.ld" "$dir/ram_copies.tsv" \
  -postScript Reanalyze.java -postScript ExportE2.java "$dir/ghidra" 2>&1 \
  | grep -E '> (SetupE2|Reanalyze|ExportE2)|ERROR' | sed -e 's/^INFO  [A-Za-z0-9]*\.java> //' -e 's/ (GhidraScript) *$//' || true
rm "$dir/$name.bin"
