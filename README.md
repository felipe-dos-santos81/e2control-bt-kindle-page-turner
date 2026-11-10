# E2 Control page turner: reverse-engineering notes

Notes and official firmware images for the **HANLINYUE E2 Control** Bluetooth page turner. All data comes from read-only probes of one unit and from the vendor's public download servers.

## Device identity

| | Value |
|---|---|
| Chip | Zhuhai Jieli **AC632N** (from the firmware header) |
| USB | `4c4a:4155` "USB Composite Device", CDC-ACM serial only (`/dev/cu.usbmodem*`), no HID |
| BLE name | `E2 Control` |
| BLE PnP ID | `05AC:0220` v`0x0110` (an Apple keyboard ID; the device spoofs it) |
| Device Information | manufacturer `zhuhai_jieli`, model `hid_mouse`, FW/HW/SW `0.0.1`, serial `000000` |
| Extra GATT service | `AE30`: `AE01` write without response, `AE02` notify |

The Device Information strings are the defaults in Jieli's SDK: [`fw-AC63_BT_SDK`](https://github.com/Jieli-Tech/fw-AC63_BT_SDK), `apps/hid/modules/bt/ble_hogp.c`.

### How it turns pages

Over Bluetooth the device is a HID with these reports:

- **Report 1:** a digitizer stylus with absolute X/Y from 0 to 1000. The device turns a page by sending a tap on the screen.
- **Reports 3–5:** consumer keys: volume up/down, next/previous track, play/pause, mute, power, menu, home.

The USB port does not send key presses. The vendor app uses it for configuration and firmware updates.

## Firmware

The [`firmware/`](firmware/) folder contains the official images and the vendor manifests. All images are raw flash images (`jl_isd.bin`) for the AC632N. The plain header shows version `0.01`. The body is scrambled; `tools/jlimg.py` unscrambles it (see [Reverse engineering](#reverse-engineering)).

| File | Vendor variant | Target |
|---|---|---|
| `e2control-touchpad-android-reader.bin` | 触摸板 (touchpad) | Android e-ink readers, motorcycle |
| `e2control-kobo.bin` | kobo版 | Kobo readers |
| `e2control-zoom-phone-tablet.bin` | 缩放版 (zoom) | Phones and tablets |
| `e2control-yuexingtong.bin` | 阅星瞳 | YueXingTong readers |
| `e2control-zoom-bundled-in-installer.bin` | 缩放版, older build | Copy inside the Windows updater |

Check the files:

```sh
cd firmware && shasum -a 256 -c SHA256SUMS
```

Each image matches a SHA-512 value in `manifest_en_US.json` or `manifest_zh_CN.json`. The manifest labels are not consistent between languages, so match files by hash, not by label. The firmware on your unit can be any of these variants.

### Where the files come from

1. The vendor download page, <https://hanlinyue.com.cn/1772613776748>, links the E2 updater: `https://cdn.hanlinyue.com.cn/hanlinyue-E2-bwjh.exe`.
2. The `.exe` is an NSIS installer for an Electron app. Unpack it with `7zz x`, then unpack `$PLUGINSDIR/app-64.7z`. You do not need to run it.
3. `resources/app.asar` names the manifests: `http://obs.hanlinyue.mxlt.top/pc-simple/e2control/firmware(<locale>).json`.
4. Each manifest entry gives a `firmwareUrl` (`.../e2control/<variant>/jl_isd.bin`) and a `firmwareSha512`.
5. The vendor flasher is `resources/extraResources/exe/UbootHid/UbootHid.exe`.

The firmware server uses plain HTTP. The hashes prove only that the files match the manifests, and the manifests also come over HTTP.

## Reverse engineering

The AC632N is Jieli's **BD19** family. Its CPU uses the **q32s** instruction set (the SDK listing says `ELF32-q32s`), not pi32v2.

### Quick start

```sh
make help                                      # list the targets
make firmware-unpack                           # decrypt + split all images into extracted/
make ghidra-score                              # check the q32s decoder against the vendor ROM listing
make ghidra-import variant=zoom-phone-tablet   # analyze one variant into ghidra/project/E2
```

`GHIDRA_HOME` defaults to `~/code/mine/ghidra-bundle/dist/ghidra_12.2_PUBLIC`; override it on the command line. `make ghidra-install` (run by `ghidra-import`) compiles the SLEIGH specs and copies `ghidra/JieLi` into `$GHIDRA_HOME/Ghidra/Processors/JieLi`. Restart the Ghidra GUI after this, then open `ghidra/project/E2.gpr`. `make ghidra-import` also writes all pseudo-C to `extracted/<variant>/ghidra/decompiled.c`.

### Layout

| Path | Content |
|---|---|
| `tools/jlimg.py` | Unpacks `jl_isd.bin`: boot table, `uboot.boot`, `isd_config.ini`, app area, `app.bin`, `cfg_tool.bin`, `layout.json`, `ram_copies.tsv`. Checks every CRC. |
| `tools/romlst.py` | Rebuilds the BD19 mask ROM (`0x100000`–`0x107000`) and its labels from the SDK's `rom.lst`. |
| `tools/q32s_score.py` | Compares Ghidra's decoding with the vendor objdump. |
| `tools/fndiff.py` | Compares variants by normalized decompiled C, per function. |
| `tools/hiddesc.py` | Prints HID report descriptor items from a flash range. |
| `ghidra/JieLi/` | Ghidra processor module (Quarkslab fork, q32s fixed here; see its README). |
| `ghidra/scripts/` | `SetupE2` (memory map, labels), `Reanalyze` (analysis to a fixed point), `ExportE2` (pseudo-C), `DisasmAt`, `DumpErrors`, `Xrefs`. |
| `sdk/` | Three reference files from the Jieli SDK. |
| `extracted/`, `ghidra/project/` | Generated. Not committed. |

### Findings

- **Image scrambling.** The header and the boot table use the Jieli ENC scrambler (CRC-16 LFSR XOR) with key `0xFFFF`. The app area starts at flash `0x3000`. Its 32-byte blocks use key `chip_key ^ (offset >> 2)`. The chip key is **`0x284c`** for all five images. `jlimg.py` finds it by brute force: only that key gives a valid CRC for the `app_area_head` record.
- **App area files.** `app.bin` (code), `cfg_tool.bin` (holds the BLE name `E2 Control`), and the partitions `VM`, `PRCT` and `BTIF`.
- **Memory map.**

  | Address | Content |
  |---|---|
  | `0x00000`–`0x0FFFF` | RAM. At boot, the startup code copies `.data` to `0x840` and RAM-resident code to `0x41E0` (kobo: `0x67A0`, touchpad: `0x4200`). |
  | `0x100000`–`0x107000` | Mask ROM. The stubs at `0x106D98`+ are listed in `sdk/maskrom_stubs.ld`. |
  | `0x1E00000` | App area. `app.bin` runs from `0x1E00120` (`CODE_BEG` in `sdk/sdk_ld.c`). |

- **Branch wrap.** The 23-bit PC-relative branches wrap at 32 MB. This is how flash code calls the ROM. With the wrap, all 282 ROM-bound calls in `app.bin` land exactly on a `maskrom_stubs.ld` label.
- **Decoder status.** All 9,746 ROM instructions decode at the right length. The zoom image gives 1,276 functions with 5 error bookmarks; kobo gives 1,555 functions with 2. Every function decompiles.
- **Known gaps.** No peripheral (SFR) register map yet. Opcodes that appear in neither the ROM nor these images are not checked. Check the vendor listing before you trust a rare instruction.

### Button behavior (zoom build)

Found by comparing the variants with `tools/fndiff.py`. Apart from relocation, `yuexingtong` and `zoom-phone-tablet` differ in a single button function. Addresses below are in `zoom-phone-tablet`.

- **Dispatcher.** `FUN_01e05e46(event, key)` uses the SDK key events (`include_lib/system/event.h`: 0 click, 1 long, 2 hold, 3 up, 4 double-click). Clicks act only when `[0x2e1f]` is 1 or 2, probably the connection state.
- **Mode.** The byte at RAM `0x8d0` selects the behavior (values 4, 5, 6). Its source (VM config or the `AE30` channel) is not traced yet.
- **Report sender.** `FUN_01e05b96(table, total, size)` sends each `size`-byte record of a flash table as one HID report (first byte = report ID), 3 OS ticks apart.
- **Two descriptor sets.** The flag `[0x2e20] == 2` selects set A (`0x1E185A4`, report 6 touch pad). Otherwise set B (`0x1E186BC`, report 1 stylus, the one seen over BLE) is used. The report 5 bit order differs between the sets. Every table checked maps to the same key in both.

| Input | Mode 4 | Mode 5 | Mode 6 |
|---|---|---|---|
| Key 1 click | volume key (report 4) | swipe left → right, X 250 → 862, Y 350 (previous page); PrevTrack when the toggle is on | swipe down, Y 250 → 750 at X 500 |
| Key 2 click | volume key (report 4) | swipe right → left (next page); NextTrack when the toggle is on | swipe up |
| Key 3 click | Menu / Home | tap at (500, 500); Play/Pause when the toggle is on | tap at (500, 500) |
| Key 3 double-click | — | double tap at (500, 500), the zoom; Play/Pause when the toggle is on | double tap |
| Key 3 long | Menu / Home | Menu / Home | Menu / Home |
| Key 7 long | — | flips the toggle (`[0x2e12]`) | — |
| Key 3 hold (183 repeats) | clears pairing, increments a byte of the BT MAC, reboots after 1 s | same | same |

Ghidra misses the click handlers' jump table (`tbb` at `0x1E05D2A`) and the gesture functions `0x1E05C02` and `0x1E05C54`. Read them in the disassembly.

## Reverse-engineering resources

- [osteotek/hlyflash](https://github.com/osteotek/hlyflash): CLI for Hanlinyue AC632N/AC636N page turners. It reads device info, parses `.ufw` files, implements the ENC cipher, and dumps flash.
- [kagaimiq/jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool): Jieli USB "UBOOT" dumper and flasher.
- [kagaimiq/jielie](https://kagaimiq.github.io/jielie/misc/cipher.html): notes on Jieli ciphers and data formats.
- [quarkslab/ghidra-jieli](https://github.com/quarkslab/ghidra-jieli): Ghidra module for pi32, pi32v2 and q32s. `ghidra/JieLi` starts from it. See also the [SSTIC 2026 paper on pi32v2](https://www.sstic.org/media/SSTIC2026/SSTIC-actes/retro-ingenierie-micrologiciel-pi32v2/SSTIC2026-Article-retro-ingenierie-micrologiciel-pi32v2-cauquil.pdf).
- [Jieli-Tech](https://github.com/Jieli-Tech): public SDKs. The Bluetooth stack ships as binary `lib.a` files only.

## Warnings

- Flash only an AC632N image. An image for a different chip, for example AC636N, can make the device unusable.
- Make a full flash dump before any write.
- Use read commands only until you have a verified backup.
