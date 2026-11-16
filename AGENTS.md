# AGENTS.md

This file provides guidance to coding agents when working with code in this repository.

## What this repo is

Two linked projects for one device, the HANLINYUE "E2 Control" Bluetooth page turner (Jieli AC632N chip, BD19 family, **q32s** CPU — not pi32v2):

1. **Reverse engineering** of the vendor firmware: unpack/decrypt the images, a Ghidra processor module for q32s, analysis scripts. Findings are in `README.md`.
2. **Own firmware** (`fw/`) that makes the remote look like Amazon's "Kindle Click", so a stock Kindle pairs with it. Designs: `docs/superpowers/specs/2026-11-09-kindle-click-firmware-design.md` (the later "Owner decisions" and "Later decisions" sections override earlier text) and `2026-11-15-brightness-mode-design.md` (the third button's mode switch; its "As built" section is current). Plans: `docs/superpowers/plans/`. Build notes and the **"Before flashing" checklist**: `fw/README.md`. Update-mode facts observed on the device: `docs/update-path.md`.

## Hard rules for the physical device

- It is the owner's only unit and there is **no recovery tool**. A bad flash can be final, and so can a firmware that is written correctly but does not start.
- Every action that writes to the device, or changes its state (for example the `JUMPBOOT` command), needs the owner's explicit go-ahead at that moment. Read-only `get` queries were approved once; ask again in a new session.
- Never flash an image built for another chip. The vendor's Kindle builds are for AC636N.
- Do not flash the custom firmware until every item of the "Before flashing" checklist in `fw/README.md` holds. The first custom flash (2026-11-10) went ahead on the owner's decision with items 4 and 5 open: the pins come from the stock firmware and are not measured on the board.
- Device state as of 2026-11-15, 21:00: **the unit holds the build with the review fixes (image SHA-256 starting `844d93be71d46da1`) and has not started since a reboot out of update mode** (`docs/update-path.md`, "Not starting again"). That build is not checked on the unit; the first brightness-mode build (`49047d7822605b87`, copy in `fw/out/e2-kindle-on-unit.bin` if still there) was: pairing as a "Kindle Click", page turns, sleep and brightness mode worked ("Brightness mode on the unit"). The unit was also locked out from 2026-11-10 to 2026-11-15 and came back by itself; the cause is unknown ("Lockout" and "Back in"). Do not push a release tag while this is open. Its stock image was the vendor's `zoom-phone-tablet` (proved by block CRCs). Check which mode the unit is in before assuming a serial port exists.
- `tools/e2boot.py enter`, `flash`, `rescue` and `reboot`, and `make fw-write`, change the device: each run needs that go-ahead. The tool refuses them without `--yes`, and the Makefile target without `reboot=1`. `info` and `verify` only read, but need the unit in update mode.
- Serial protocol: JSON at 460800 baud on `/dev/cu.usbmodem*` (on macOS set the speed with the `IOSSIOSPEED` ioctl; `pyserial` is not installed). The stock firmware sometimes drops the first command after the port opens, so send a `get` first.
- Firmware images (`firmware/*.bin`) and vendor `.ufw` files stay out of git; only hashes and manifests are committed.

## Commands

`make help` lists all targets. `GHIDRA_HOME` defaults to `~/code/mine/ghidra-bundle/dist/ghidra_12.2_PUBLIC`.

```sh
# reverse engineering
make firmware-unpack                  # firmware/*.bin -> extracted/<variant>/ (needs the images in firmware/)
make ghidra-score                     # q32s decoder vs the vendor ROM listing; must stay 9746/9746
make ghidra-import variant=kobo       # analyze one variant into ghidra/project/E2, pseudo-C to extracted/<variant>/ghidra/decompiled.c

# own firmware
make test                             # all host tests (seconds): pure C logic, image packer (needs firmware/*.bin), updater client
make fw-build                         # Docker build -> fw/out/app.bin (~20 s), then packed into firmware/e2control-$(variant).bin -> fw/out/e2-kindle.bin
make fw-write reboot=1                # WRITES TO THE DEVICE: wait for the E2 on USB, flash fw/out/e2-kindle.bin, reboot
python3 -I tools/e2boot.py            # client for the unit's USB updater: enter, info, verify, flash, reboot, rescue
```

- Single tests: there is no test runner. `fw/tests/test_kc.c` has one `test_*` function per unit, called from `main`; `tools/test_jlimg.py` runs every `test_*` function. To run one, call it alone from `main` / the `__main__` block temporarily, or run the file and read the `ok <name>` lines.
- Run project Python with `python3 -I` (the Makefile does). Tools are stdlib-only, single-file scripts. One exception: `tools/e2boot.py` loads the `hidapi` library through `ctypes` (`brew install hidapi`).
- In this user's shell `cc` is aliased to something else. In scripts and one-off commands use `$(CC)` (Makefile) or `/usr/bin/cc`.
- `extracted/` and `ghidra/project/` are generated and may be absent (they were deleted on 2026-11-15). `make ghidra-import variant=zoom-phone-tablet` rebuilds them; it takes a while.
- Query the Ghidra project headless and read-only, one process at a time:
  `$GHIDRA_HOME/support/analyzeHeadless ghidra/project E2 -process <variant>.bin -noanalysis -readOnly -scriptPath ghidra/scripts -postScript Xrefs.java <hexaddr>`
- After editing any `.sinc`/`.slaspec`, run `make ghidra-score` (it recompiles and re-installs the module). The module is **copied** into Ghidra; a symlink fails Ghidra's path-case check.

## Architecture

### Image format and tooling (`tools/jlimg.py`)

`jl_isd.bin` = header + boot table (scrambled with key `0xFFFF`) + `uboot.boot` + app area at flash `0x3000` (32-byte blocks scrambled with `chip_key ^ (offset >> 2)`, chip key `0x284c`). The app area holds `app.bin` (code, loaded at `0x1E00120`) and `cfg_tool.bin`. `unpack` splits and checks every CRC; `pack` replaces only `app.bin`/`cfg_tool.bin` inside a base image and self-checks the result. The vendor image files end where the `VM` partition starts; a real device dump is longer, and `pack` copies that tail unchanged. Memory map: RAM `0x0–0xFFFF` (startup copies `.data` and RAM code there), mask ROM `0x100000`, flash `0x1E00000`.

### Updater client (`tools/e2boot.py`)

The unit's boot code holds a USB HID updater, reached with `JUMPBOOT` on the serial port. It can erase, write and report a CRC per 4 KB block; it cannot read flash. `e2boot.py` speaks that protocol (packet layout and commands: `docs/update-path.md`). `flash` writes only from the app area's start, refuses an image whose boot area differs from the device's, erases block 0 first and writes its first 32 bytes last, so an interrupted run comes back in update mode. `tools/test_e2boot.py` runs it against a model of the boot code's handlers; keep that model in step with `docs/update-path.md`.

### Ghidra side (`ghidra/`)

`ghidra/JieLi` is a fork of Quarkslab's module with q32s fixes, checked against `sdk/rom.lst` (the vendor's own ROM disassembly, used as ground truth by `tools/romlst.py` + `tools/q32s_score.py`). `SetupE2.java` builds the memory map and labels, `Reanalyze.java` iterates analysis to a fixed point (one pass misses code reached only through function pointers), `ExportE2.java` writes pseudo-C. Ghidra still misses some jump-table targets; disassemble by address when a function seems absent.

### Own firmware (`fw/`)

An **overlay on Jieli's public SDK** (`Jieli-Tech/fw-AC63_BT_SDK`, pinned commit in `fw/build.sh`), not a standalone program:

- `fw/build.sh` clones the SDK into `fw/sdk/`, **hard-resets and cleans it on every build**, copies `fw/src/` into the SDK tree, applies `fw/patches/*.patch` in name order, builds in Docker, then checks the result (size, first instructions, load address). Never edit `fw/sdk/` by hand: SDK changes go into the patches (regenerate a patch from a diff of the SDK tree). `0005-e2-board.patch` holds the E2's pins, read from the stock firmware (table in `fw/README.md`).
- **Two kinds of source files, kept apart on purpose:**
  - `fw/src/kc_*.c/.h` — pure C99, no SDK includes, host-tested in `fw/tests/test_kc.c`. Protocol bytes (`kc_proto`), key→action, the page/brightness mode and its 15 s return (`kc_keys`), link-ready gate and press queue (`kc_link`), power-off decision (`kc_power`), update-command matcher (`kc_update_cmd`). `make test` globs `kc_*.c`, so an SDK-bound file must not use that prefix.
  - `fw/src/app_*.c`, `fw/src/e2_*.c` — SDK-bound glue: BLE events and keys (`app_kindle_click`), report sending (`app_kindle_send`), USB update entry (`e2_update`), power policy (`e2_power`). They only compile inside the container; editor diagnostics about missing SDK headers are noise.
- The GATT table is generated by `fw/att_table.py` (`gen` writes the SDK profile header used in patch 0003, `dump fw/out/app.bin` reads the table back from a built image). The Kindle identifies the remote by exact bytes (manufacturer data `71 01 04 43`, PnP ID `01 71 01 43 04 00 01`, one keyboard+consumer input report with ID 1); do not change them without the spec.
- **Constraints that shape the code** (reasons are in the spec and `fw/README.md`):
  - The update entry must survive: USB CDC stays started as in the stock firmware, and `JUMPBOOT` on the serial port resets into the boot-area updater. With USB started the chip cannot sleep, so the app is designed to power the unit off instead (180 s unconnected, 30 min idle while connected), never while the USB-power sense pin is high. That policy is off (`TCFG_E2_SELF_POWER_OFF 0` in the board config, `patches/0005`) until wake-up from power-off is proven on the unit. The SDK's battery detect is off too (`TCFG_SYS_LVD_EN 0`): its check at start can power the unit off before USB starts. (The chip's VBAT pin does read the battery on this board; the stock firmware measures it on PA8.)
  - **Nothing may power the unit off, or block, before `e2_update_usb_start()` runs.** The update command is the only way back in. The SDK's battery check at start can do exactly that; it was the first suspect for the 2026-11-10 lockout, whose cause is still unknown. `TCFG_SYS_LVD_EN 0` switches it off. Treat every SDK default that can call `power_set_soft_poweroff` as suspect until it is seen working on the unit.
  - Send `?` on the serial port for one status line (`vbat`, `pa8`, `pa1`, `usb`, `pb6`, `up`): use it to measure a board fact before the code depends on it.
  - The BD19 Bluetooth library has no LE Secure Connections; pairing is legacy Just Works. The Kindle Colorsoft accepts it (seen 2026-11-15).
  - Board pins are read from the stock firmware and held in board-config macros. The three keys and the USB-power sense work on the unit, and the owner's checks report the LED working; the wake-up pins are untested.

## Releases

- A tag `vX.Y.Z` pushed to GitHub runs `.github/workflows/release.yml`: host tests, `fw/build.sh`, `tools/release/package.sh`, then a GitHub Release with `e2-kindle-click-<version>.zip` and `SHA256SUMS`. A tag with a suffix (`v1.1.0-rc1`) makes a pre-release. A manual run of the workflow builds the archive as a workflow artifact and publishes nothing.
- The archive holds `app.bin`, `jlimg.py`, `e2boot.py`, `README.txt` and the two licenses. It never holds a vendor image: `package.sh` fails on any other file. `tools/release/README.txt` is what the user reads, and it becomes the release notes; keep it in step with the firmware's behavior.
- The repo is public and a release is public at once. Pushing a tag needs the owner's ask. Tag only sources that have run on the unit: `README.txt` says they have.
- Check a change locally with `actionlint`, `shellcheck tools/release/package.sh` and `make fw-build && tools/release/package.sh 0.0.0-dev` (writes `dist/`). CI has no vendor images, so `tools/test_jlimg.py` skips there.

## Conventions

- Makefile targets carry `## [STEP N] …` help comments and are listed in `.PHONY`; firmware targets use the `fw-` prefix. Keep the target list short: `variant=` (default `zoom-phone-tablet`, the owner's unit) selects the image for `ghidra-import` and `fw-build`.
- Docs are written in plain, short sentences and separate **observed / inferred / unknown**; keep that when adding findings.
- Work on `main` (the only branch). Commit only when asked.
- Subagent tasks that dig deep into the vendor firmware have twice been stopped by model safeguards; keep such tasks narrow and framed as plain SDK work.
