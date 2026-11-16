# E2 Control page turner: Kindle firmware and reverse engineering

Two things for the **HANLINYUE E2 Control** Bluetooth page turner (Jieli AC632N chip):

1. **Own firmware** that makes the remote look like Amazon's Kindle Click, so a stock Kindle pairs with it. It worked on a Kindle Colorsoft on 2026-11-15; the test unit has since stopped starting (see [Status](#status-and-open-points)).
2. **Reverse-engineering notes and tools** for the vendor firmware. The own firmware is built on them.

> **Warning:** flashing this firmware can permanently damage the "E2 Control" device. There is no recovery tool. Use it at your own risk, and read [Before you flash](#before-you-flash) first.

The data comes from probes of one unit and from the vendor's public download servers.

```sh
make help    # list all targets
make test    # run all host tests (no device, no Docker)
```

## Kindle firmware

A stock Kindle (2024 models onward, software 5.20.1.0.2) accepts only one page turner: Amazon's Kindle Click, which it recognizes by its Bluetooth IDs. This firmware presents those IDs and sends the Kindle Click's keys. The Kindle lists the remote as a "Kindle Click".

| Input | Page mode | Brightness mode |
|---|---|---|
| Next / previous, short press | next / previous page | one step brighter / dimmer |
| Next / previous, held | nothing | keeps changing the brightness |
| Third button, short press | go to brightness mode | go to page mode |
| Third button, long press | Kindle sleeps | Kindle sleeps |
| Third button, held about 10 s | forget the pairing | forget the pairing |

The remote starts in page mode. Brightness mode ends 15 s after the last press; the LED is steady on while it lasts. The side selector does nothing.

### Pairing again

Clear the pairing on both sides. If only one side forgets, the other still holds the old key and the new pairing can fail.

1. On the Kindle, remove the "Kindle Click" from its Bluetooth devices.
2. On the remote, hold the third button for about 10 s, until the LED goes back to its slow flash. The remote forgets the pairing and advertises again. If the Kindle is connected, it goes to sleep after the first second of the hold; keep holding, then wake it.
3. On the Kindle, pair again. It lists the remote as a "Kindle Click".

Flashing keeps the pairing, so this is not needed after an update. The 10 s hold is not yet checked on the unit.

### Download

Each [release](https://github.com/felipe-dos-santos81/e2control-bt-kindle-page-turner/releases) holds the compiled app (`app.bin`), the two tools that pack and flash it, and a `README.txt` with the steps. It holds no ready-to-flash image: you pack the app into the vendor image of your own unit. The flash tool is written for macOS. No release is published yet.

### Before you flash

- There is no recovery tool for this unit. A firmware that is written correctly but does not start locks you out. The test unit has stopped starting twice, the first time for five days. The cause is unknown.
- Flash only an AC632N image built on your unit's own image. An image for another chip can make the device unusable. The vendor's Kindle builds for other models are AC636N images. `e2boot.py flash` refuses an image whose boot area differs from the device's.
- The updater cannot read flash, so there is no dump. Before the first write, find your variant with `e2boot.py verify` and keep that image. Pairing data and settings are not saved.
- Go through the "Before flashing" checklist in [`fw/README.md`](fw/README.md).

### Build and flash from source

```sh
make fw-build variant=<your unit's variant>   # build the app (Docker) and pack it into that variant's image: fw/out/e2-kindle.bin
make fw-write reboot=1                        # WRITES TO THE DEVICE: wait for the E2 on USB, flash, reboot
```

`variant` defaults to `zoom-phone-tablet`; `make help` lists the others. `fw-write` refuses to run without `reboot=1`.

The steps one at a time, with `tools/e2boot.py` (needs `brew install hidapi`):

```sh
python3 -I tools/e2boot.py enter /dev/cu.usbmodemXXXX --yes   # normal mode -> update mode
python3 -I tools/e2boot.py verify <image>                     # read-only: which blocks differ from <image>
python3 -I tools/e2boot.py flash fw/out/e2-kindle.bin --yes
python3 -I tools/e2boot.py reboot --yes
```

To go back to the stock firmware, flash your unit's vendor image the same way.

### Status and open points

Checked on the unit (2026-11-15): pairing, page turns, sleep, brightness mode, and the update path from the own firmware. Not checked: the changes made after that, among them the third button read as a click-only key and the 10 s hold.

- **The unit does not start.** Since the evening of 2026-11-15 it shows no USB device, no LED and no Bluetooth. The same happened on 2026-11-10, and it came back by itself five days later. Both times the side selector had been moved or the remote switched off. The cause is unknown; [`docs/update-path.md`](docs/update-path.md) has the record. The only other way in is the chip ROM's USB download mode, which needs a forced-download tool.
- **USB starts late.** The firmware starts USB after the Bluetooth stack, so a start that hangs earlier cannot be rescued over USB. To be moved to the very start.
- **Power-off and wake-up.** Untested. The firmware never switches the unit off, so the battery drains while it is on.
- **Battery level.** The Kindle is told a fixed 88 %.
- **Board pins.** They come from the stock firmware's tables (`fw/patches/0005-e2-board.patch`). The keys, the LED and the USB-power sense behave as expected; the wake-up pins and the side selector are untested.

| Document | Content |
|---|---|
| [`fw/README.md`](fw/README.md) | Build notes, buttons, board pins, power behavior, "Before flashing" checklist |
| [`docs/update-path.md`](docs/update-path.md) | The updater's protocol, and everything observed on the device |
| [Kindle Click design](docs/superpowers/specs/2026-11-09-kindle-click-firmware-design.md) | What the Kindle requires, behavior of the remote, owner decisions |
| [Brightness mode design](docs/superpowers/specs/2026-11-15-brightness-mode-design.md) | The third button's mode switch |
| [`docs/superpowers/plans/`](docs/superpowers/plans/) | The task plans for both designs |

## The stock device

| | Value |
|---|---|
| Chip | Zhuhai Jieli **AC632N** (from the firmware header) |
| USB, normal mode | `4c4a:4155` "USB Composite Device", CDC-ACM serial only (`/dev/cu.usbmodem*`) |
| USB, update mode | same ID, one vendor HID interface (usage page `0xFF00`), no serial port |
| USB serial protocol | JSON at 460800 baud: `{"o":"get","t":"model"}` → `{"o":"getresult","t":"model","v":"JL_BT"}`. Fields: `model`, `firmware` (`V0.04`), `mac`, `battery`. The first command after the port opens is sometimes dropped. |
| BLE name | `E2 Control` |
| BLE PnP ID | `05AC:0220` v`0x0110` (an Apple keyboard ID; the device spoofs it) |
| Device Information | manufacturer `zhuhai_jieli`, model `hid_mouse`, FW/HW/SW `0.0.1`, serial `000000` |
| Extra GATT service | `AE30`: `AE01` write without response, `AE02` notify |

The Device Information strings are the defaults in Jieli's SDK: [`fw-AC63_BT_SDK`](https://github.com/Jieli-Tech/fw-AC63_BT_SDK), `apps/hid/modules/bt/ble_hogp.c`.

### How the stock firmware turns pages

Over Bluetooth the device is a HID with these reports:

- **Report 1:** a digitizer stylus with absolute X/Y from 0 to 1000. The device turns a page by sending a tap on the screen.
- **Reports 3–5:** consumer keys: volume up/down, next/previous track, play/pause, mute, power, menu, home.

The USB port does not send key presses. The vendor app uses it for configuration and firmware updates. The command `{"o":"JUMPBOOT"}` puts the device in update mode; see [`docs/update-path.md`](docs/update-path.md).

## Vendor firmware images

The [`firmware/`](firmware/) folder holds the vendor manifests and `SHA256SUMS`. The images are not in git; download them as described below. All are raw flash images (`jl_isd.bin`) for the AC632N. The body is scrambled; `tools/jlimg.py` unscrambles it.

| File | Vendor variant | Target |
|---|---|---|
| `e2control-touchpad-android-reader.bin` | 触摸板 (touchpad) | Android e-ink readers, motorcycle |
| `e2control-kobo.bin` | kobo版 | Kobo readers |
| `e2control-zoom-phone-tablet.bin` | 缩放版 (zoom) | Phones and tablets |
| `e2control-yuexingtong.bin` | 阅星瞳 | YueXingTong readers |
| `e2control-zoom-bundled-in-installer.bin` | 缩放版, older build | Copy inside the Windows updater |

```sh
cd firmware && shasum -a 256 -c SHA256SUMS
```

Each image matches a SHA-512 value in `manifest_en_US.json` or `manifest_zh_CN.json`. The manifest labels differ between languages, so match files by hash. All five variants report version `V0.04` over USB, so the version does not tell them apart. `tools/e2boot.py verify <image>` does: it compares block checksums through the unit's updater. The unit studied here ran `zoom-phone-tablet`.

Where the files come from:

1. The vendor download page, <https://hanlinyue.com.cn/1772613776748>, links the E2 updater: `https://cdn.hanlinyue.com.cn/hanlinyue-E2-bwjh.exe`.
2. The `.exe` is an NSIS installer for an Electron app. Unpack it with `7zz x`, then unpack `$PLUGINSDIR/app-64.7z`. You do not need to run it.
3. `resources/app.asar` names the manifests: `http://obs.hanlinyue.mxlt.top/pc-simple/e2control/firmware(<locale>).json`.
4. Each manifest entry gives a `firmwareUrl` (`.../e2control/<variant>/jl_isd.bin`) and a `firmwareSha512`.
5. The vendor flasher is `resources/extraResources/exe/UbootHid/UbootHid.exe`.

The firmware server uses plain HTTP. The hashes prove only that the files match the manifests, and the manifests also come over HTTP.

## Reverse engineering

The AC632N is Jieli's **BD19** family. Its CPU uses the **q32s** instruction set (the SDK listing says `ELF32-q32s`), not pi32v2.

```sh
make firmware-unpack                           # decrypt + split all images into extracted/
make ghidra-score                              # check the q32s decoder against the vendor ROM listing
make ghidra-import variant=zoom-phone-tablet   # analyze one variant into ghidra/project/E2
```

`GHIDRA_HOME` defaults to `~/code/mine/ghidra-bundle/dist/ghidra_12.2_PUBLIC`; override it on the command line. `make ghidra-import` compiles the SLEIGH specs, copies `ghidra/JieLi` into `$GHIDRA_HOME/Ghidra/Processors/JieLi`, and writes all pseudo-C to `extracted/<variant>/ghidra/decompiled.c`. Restart the Ghidra GUI after it, then open `ghidra/project/E2.gpr`.

### Findings

- **Image scrambling.** The header and the boot table use the Jieli ENC scrambler (CRC-16 LFSR XOR) with key `0xFFFF`. The app area starts at flash `0x3000`. Its 32-byte blocks use key `chip_key ^ (offset >> 2)`. The chip key is **`0x284c`** for all five images. `jlimg.py` finds it by brute force: only that key gives a valid CRC for the `app_area_head` record.
- **App area files.** `app.bin` (code), `cfg_tool.bin` (holds the BLE name `E2 Control`), and the partitions `VM`, `PRCT` and `BTIF`.
- **Boot code.** `uboot.boot` is one LZ4 block whose dictionary is the mask ROM. It unpacks to `0x2940` bytes at RAM `0x2000` and holds the USB HID updater. The updater can erase, write and report block CRCs. It cannot read flash.
- **Memory map.**

  | Address | Content |
  |---|---|
  | `0x00000`–`0x0FFFF` | RAM. At boot, the startup code copies `.data` to `0x840` and RAM-resident code to `0x41E0` (kobo: `0x67A0`, touchpad: `0x4200`). |
  | `0x100000`–`0x107000` | Mask ROM. The stubs at `0x106D98`+ are listed in `sdk/maskrom_stubs.ld`. |
  | `0x1E00000` | App area. `app.bin` runs from `0x1E00120` (`CODE_BEG` in the SDK's `cpu/bd19/sdk_ld.c`). |

- **Branch wrap.** The 23-bit PC-relative branches wrap at 32 MB. This is how flash code calls the ROM. With the wrap, all 282 ROM-bound calls in `app.bin` land exactly on a `maskrom_stubs.ld` label.
- **Decoder status.** All 9,746 ROM instructions decode at the right length. The zoom image gives 1,276 functions with 5 error bookmarks; kobo gives 1,555 functions with 2. Every function decompiles.
- **Known gaps.** No peripheral (SFR) register map yet. Opcodes that appear in neither the ROM nor these images are not checked. Check the vendor listing before you trust a rare instruction.

### Button behavior of the stock firmware (zoom build)

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

## Repository layout

| Path | Content |
|---|---|
| `fw/` | Own firmware: sources, SDK patches, container build. See [`fw/README.md`](fw/README.md). |
| `tools/jlimg.py` | Unpacks `jl_isd.bin` and checks every CRC. `pack` puts a new `app.bin` into an existing image and keeps all other bytes. |
| `tools/e2boot.py` | Client for the unit's USB updater: enter update mode, compare the flash with an image by block CRC, flash, reboot. |
| `tools/romlst.py`, `tools/q32s_score.py` | Rebuild the BD19 mask ROM from the SDK's `rom.lst`; compare Ghidra's decoding with it. |
| `tools/fndiff.py`, `tools/hiddesc.py` | Compare variants by decompiled C, per function; print HID report descriptors from a flash range. |
| `tools/release/`, `.github/workflows/release.yml` | The release: a version tag builds the app on GitHub and publishes it. |
| `ghidra/JieLi/` | Ghidra processor module (Quarkslab fork, q32s fixed here; see its README). |
| `ghidra/scripts/` | `SetupE2` (memory map, labels), `Reanalyze` (analysis to a fixed point), `ExportE2` (pseudo-C), `DisasmAt`, `DumpErrors`, `Xrefs`. |
| `sdk/` | Two reference files from the Jieli SDK. |
| `firmware/` | Vendor manifests and hashes. The images are not committed. |
| `docs/` | Designs, plans, and the device record. |
| `extracted/`, `ghidra/project/`, `fw/sdk/`, `fw/out/`, `dist/` | Generated. Not committed. `make clean` removes all but `fw/sdk/`. |

## Resources

- [osteotek/hlyflash](https://github.com/osteotek/hlyflash): CLI for Hanlinyue AC632N/AC636N page turners. It reads device info, parses `.ufw` files, implements the ENC cipher, and dumps flash.
- [kagaimiq/jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool): Jieli USB "UBOOT" dumper and flasher.
- [kagaimiq/jielie](https://kagaimiq.github.io/jielie/misc/cipher.html): notes on Jieli ciphers and data formats.
- [quarkslab/ghidra-jieli](https://github.com/quarkslab/ghidra-jieli): Ghidra module for pi32, pi32v2 and q32s. `ghidra/JieLi` starts from it. See also the [SSTIC 2026 paper on pi32v2](https://www.sstic.org/media/SSTIC2026/SSTIC-actes/retro-ingenierie-micrologiciel-pi32v2/SSTIC2026-Article-retro-ingenierie-micrologiciel-pi32v2-cauquil.pdf).
- [Jieli-Tech](https://github.com/Jieli-Tech): public SDKs. The Bluetooth stack ships as binary `lib.a` files only.

## License

The project's own code is under the [MIT License](LICENSE). Parts from others keep their licenses: `ghidra/JieLi` is a fork of quarkslab/ghidra-jieli (Apache 2.0, [`ghidra/JieLi/LICENSE`](ghidra/JieLi/LICENSE)); `sdk/` holds two files from Jieli's SDK (Apache 2.0); a built `app.bin` contains code from that SDK. The vendor's firmware images are not included and remain the property of their rights holders. This project is not affiliated with Amazon, HANLINYUE or Jieli.
