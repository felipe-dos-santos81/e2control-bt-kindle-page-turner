# SDK reference files

Copied without changes from [Jieli-Tech/fw-AC63_BT_SDK](https://github.com/Jieli-Tech/fw-AC63_BT_SDK) at commit `fdc018d` (Apache-2.0):

| File | SDK path | Use |
|---|---|---|
| `rom.lst` | `cpu/bd19/tools/rom.lst` | Vendor q32s objdump of the BD19 mask ROM. `tools/romlst.py` rebuilds the ROM bytes from it and uses it as ground truth for the Ghidra decoder. |
| `maskrom_stubs.ld` | `cpu/bd19/maskrom_stubs.ld` | Addresses of the ROM functions the app calls. |
