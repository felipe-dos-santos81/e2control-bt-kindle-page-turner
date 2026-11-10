#!/bin/sh
# Compile the JieLi SLEIGH specs and copy the module into Ghidra.
# Re-run after editing any .sinc/.slaspec. (A symlink fails Ghidra's path-case check.)
# usage: GHIDRA_HOME=/path/to/ghidra_12.x_PUBLIC ghidra/setup.sh
set -eu
: "${GHIDRA_HOME:?set GHIDRA_HOME}"
here=$(cd "$(dirname "$0")" && pwd)
"$GHIDRA_HOME/support/sleigh" -a "$here/JieLi/data/languages"
rm -rf "$GHIDRA_HOME/Ghidra/Processors/JieLi"
cp -R "$here/JieLi" "$GHIDRA_HOME/Ghidra/Processors/JieLi"
echo "installed $GHIDRA_HOME/Ghidra/Processors/JieLi (restart the Ghidra GUI to load it)"
