# AGENTS.md

This file provides guidance to coding agents when working with code in this repository.

## What this repo is

Two linked projects for one device, the HANLINYUE "E2 Control" Bluetooth page turner (Jieli AC632N chip, BD19 family, **q32s** CPU — not pi32v2):

1. **Reverse engineering** of the vendor firmware: unpack/decrypt the images, a Ghidra processor module for q32s, analysis scripts. Findings are in `README.md`.
2. **Own firmware** (`fw/`) that makes the remote look like Amazon's "Kindle Click", so a stock Kindle pairs with it. Design: `docs/superpowers/specs/2026-11-09-kindle-click-firmware-design.md` (the later "Owner decisions" and "Later decisions" sections override earlier text). Plan: `docs/superpowers/plans/2026-11-09-kindle-click-firmware.md`. Build notes and the **"Before flashing" checklist**: `fw/README.md`. Update-mode facts observed on the device: `docs/update-path.md`.

## Hard rules for the physical device

- It is the owner's only unit and there is **no recovery tool**. A bad flash can be final.
- Every action that writes to the device, or changes its state (for example the `JUMPBOOT` command), needs the owner's explicit go-ahead at that moment. Read-only `get` queries were approved once; ask again in a new session.
- Never flash an image built for another chip. The vendor's Kindle builds are for AC636N.
- Do not flash the custom firmware until every item of the "Before flashing" checklist in `fw/README.md` holds. As of 2026-11-10 the pins come from the stock firmware and are not measured on the board; no write has been made to the device yet.
- Device state as of 2026-11-10: **the custom firmware is on the unit and the unit is not reachable**: it has not started since it was switched off (see "Lockout" in `docs/update-path.md`). The first flash and the update path from it worked before that. Before that: stock firmware `V0.04`. `get` queries and one `JUMPBOOT` were run on it (results in `docs/update-path.md`). It runs the vendor's `zoom-phone-tablet` image (proved by block CRCs with `tools/e2boot.py verify`; one block before `VM` differs). `tools/e2boot.py reboot` leaves update mode. Check which mode it is in before assuming a serial port exists.
- Serial protocol: JSON at 460800 baud on `/dev/cu.usbmodem*` (on macOS set the speed with the `IOSSIOSPEED` ioctl; `pyserial` is not installed). The stock firmware sometimes drops the first command after the port opens, so send a `get` first.
- Firmware images (`firmware/*.bin`), flash backups (`backup/`) and vendor `.ufw` files stay out of git; only hashes and manifests are committed.

## Commands

`make help` lists all targets. `GHIDRA_HOME` defaults to `~/code/mine/ghidra-bundle/dist/ghidra_12.2_PUBLIC`.

```sh
# reverse engineering
make firmware-unpack                  # firmware/*.bin -> extracted/<variant>/ (needs the images in firmware/)
make ghidra-score                     # q32s decoder vs the vendor ROM listing; must stay 9746/9746
make ghidra-import variant=kobo       # analyze one variant into ghidra/project/E2, pseudo-C to extracted/<variant>/ghidra/decompiled.c

# own firmware
make test                             # fw-test + tools-test
make fw-test                          # host tests for the pure C logic (seconds)
make tools-test                       # image-packer tests (needs firmware/*.bin)
make fw-build                         # Docker: Jieli toolchain container -> fw/out/app.bin (~20 s after the first image build)
make fw-image base=<flash image>      # put fw/out/app.bin into an existing image, everything else byte-identical
```

- Single tests: there is no test runner. `fw/tests/test_kc.c` has one `test_*` function per unit, called from `main`; `tools/test_jlimg.py` runs every `test_*` function. To run one, call it alone from `main` / the `__main__` block temporarily, or run the file and read the `ok <name>` lines.
- Run project Python with `python3 -I` (the Makefile does). Tools are stdlib-only, single-file scripts.
- In this user's shell `cc` is aliased to something else. In scripts and one-off commands use `$(CC)` (Makefile) or `/usr/bin/cc`.
- Query the Ghidra project headless and read-only, one process at a time:
  `$GHIDRA_HOME/support/analyzeHeadless ghidra/project E2 -process <variant>.bin -noanalysis -readOnly -scriptPath ghidra/scripts -postScript Xrefs.java <hexaddr>`
- After editing any `.sinc`/`.slaspec`, run `make ghidra-score` (it recompiles and re-installs the module). The module is **copied** into Ghidra; a symlink fails Ghidra's path-case check.

## Architecture

### Image format and tooling (`tools/jlimg.py`)

`jl_isd.bin` = header + boot table (scrambled with key `0xFFFF`) + `uboot.boot` + app area at flash `0x3000` (32-byte blocks scrambled with `chip_key ^ (offset >> 2)`, chip key `0x284c`). The app area holds `app.bin` (code, loaded at `0x1E00120`) and `cfg_tool.bin`. `unpack` splits and checks every CRC; `pack` replaces only `app.bin`/`cfg_tool.bin` inside a base image and self-checks the result. The vendor image files end where the `VM` partition starts; a real device dump is longer, and `pack` copies that tail unchanged. Memory map: RAM `0x0–0xFFFF` (startup copies `.data` and RAM code there), mask ROM `0x100000`, flash `0x1E00000`.

### Ghidra side (`ghidra/`)

`ghidra/JieLi` is a fork of Quarkslab's module with q32s fixes, checked against `sdk/rom.lst` (the vendor's own ROM disassembly, used as ground truth by `tools/romlst.py` + `tools/q32s_score.py`). `SetupE2.java` builds the memory map and labels, `Reanalyze.java` iterates analysis to a fixed point (one pass misses code reached only through function pointers), `ExportE2.java` writes pseudo-C. Ghidra still misses some jump-table targets; disassemble by address when a function seems absent.

### Own firmware (`fw/`)

An **overlay on Jieli's public SDK** (`Jieli-Tech/fw-AC63_BT_SDK`, pinned commit in `fw/build.sh`), not a standalone program:

- `fw/build.sh` clones the SDK into `fw/sdk/`, **hard-resets and cleans it on every build**, copies `fw/src/` into the SDK tree, applies `fw/patches/*.patch` in name order, builds in Docker, then checks the result (size, first instructions, load address). Never edit `fw/sdk/` by hand: SDK changes go into the patches (regenerate a patch from a diff of the SDK tree). `0005-e2-board.patch` holds the E2's pins, read from the stock firmware (table in `fw/README.md`).
- **Two kinds of source files, kept apart on purpose:**
  - `fw/src/kc_*.c/.h` — pure C99, no SDK includes, host-tested in `fw/tests/test_kc.c`. Protocol bytes (`kc_proto`), key→action (`kc_keys`), link-ready gate and press queue (`kc_link`), power-off decision (`kc_power`), update-command matcher (`kc_update_cmd`). `make fw-test` globs `kc_*.c`, so an SDK-bound file must not use that prefix.
  - `fw/src/app_*.c`, `fw/src/e2_*.c` — SDK-bound glue: BLE events and keys (`app_kindle_click`), report sending (`app_kindle_send`), USB update entry (`e2_update`), power policy (`e2_power`). They only compile inside the container; editor diagnostics about missing SDK headers are noise.
- The GATT table is generated by `fw/att_table.py` (`gen` writes the SDK profile header used in patch 0003, `dump fw/out/app.bin` reads the table back from a built image). The Kindle identifies the remote by exact bytes (manufacturer data `71 01 04 43`, PnP ID `01 71 01 43 04 00 01`, one keyboard+consumer input report with ID 1); do not change them without the spec.
- **Constraints that shape the code** (reasons are in the spec and `fw/README.md`):
  - The update entry must survive: USB CDC stays started as in the stock firmware, and `JUMPBOOT` on the serial port resets into the boot-area updater. With USB started the chip cannot sleep, so the app powers the unit off instead (180 s unconnected, 30 min idle while connected), never while the USB-power sense pin is high.
  - The BD19 Bluetooth library has no LE Secure Connections; pairing is legacy Just Works. Whether the Kindle accepts it is untested.
  - Board pins (keys, USB-power sense, wake-up) are read from the stock firmware, held in board-config macros, not measured on the board.

## Conventions

- Makefile targets carry `## [STEP N] …` help comments and are listed in `.PHONY`; firmware targets use the `fw-` prefix.
- Docs are written in plain, short sentences and separate **observed / inferred / unknown**; keep that when adding findings.
- Work on `main` (the only branch). Commit only when asked.
- Subagent tasks that dig deep into the vendor firmware have twice been stopped by model safeguards; keep such tasks narrow and framed as plain SDK work.
