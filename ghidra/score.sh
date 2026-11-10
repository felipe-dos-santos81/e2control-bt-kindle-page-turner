#!/bin/sh
# Rebuild the q32s module and score it against the vendor ROM listing.
# usage: GHIDRA_HOME=... ghidra/score.sh   (needs extracted/rom from tools/romlst.py)
set -eu
: "${GHIDRA_HOME:?set GHIDRA_HOME}"
root=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
"$root/ghidra/setup.sh" >/dev/null 2>&1
"$GHIDRA_HOME/support/analyzeHeadless" "$tmp" score -import "$root/extracted/rom/rom.bin" \
  -noanalysis -deleteProject -processor q32s:LE:32:default -loader BinaryLoader -loader-baseAddr 0x100000 \
  -scriptPath "$root/ghidra/scripts" -postScript DisasmAt.java "$root/extracted/rom/rom_insns.tsv" "$tmp/got.tsv" >"$tmp/log" 2>&1 \
  || { tail -20 "$tmp/log"; exit 1; }
python3 -I "$root/tools/q32s_score.py" "$root/extracted/rom/rom_insns.tsv" "$tmp/got.tsv" "${1:-30}"
rm -rf "$tmp"
