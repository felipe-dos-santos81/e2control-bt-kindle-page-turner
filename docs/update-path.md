# E2 Control: how update mode is entered

Status: static analysis only (2026-11-10). **Nothing here is confirmed on the device.** Sources: the vendor's Windows updater, the five stock firmware images, the Jieli SDK, and hlyflash's source. The analysis was done on the stock images, not on a dump of the owner's unit.

This is a short summary. A longer write-up was not produced.

## Findings

1. **Entry command.** The vendor updater sends one JSON command, `{"o":"JUMPBOOT"}`, over the USB serial port (`4c4a:4155`, 460800 baud). It then starts its flasher, `UbootHid.exe`.
2. **What the stock app does.** The app's USB serial handler writes a tag in RAM and resets the chip. This is the same in all five stock variants.
3. **What does the flashing.** After the reset, code in the boot area (`uboot.boot`, identical in all five variants) runs a USB HID updater. This is the device `UbootHid.exe` talks to.
4. **hlyflash does not apply as-is.** `hlyflash probe --enter-boot` uses a USB mass-storage command. The E2 has no mass-storage interface.
5. **ROM download mode.** The chip's mask ROM has its own USB download mode. A Jieli forced-download tool starts it. It is also reported to start when the flash does not boot. It needs the SDK's `bd19loader.bin`.

## What this means

- Entry into update mode is **firmware-assisted**: the app must handle the command. A custom app that does not handle it cannot be updated again over USB by this route.
- The HID updater is in the boot area. A flash that replaces only the app (as `tools/jlimg.py pack` does) keeps it.
- So the custom firmware must keep the USB serial device and the `JUMPBOOT` command. This is plan Task 10, and it is required.
- ROM download mode is the last line of defense. It needs extra hardware and is untested here.

## Observed on the device (2026-11-10)

Read-only `get` queries over `/dev/cu.usbmodem1301` at 460800 baud, with the stock firmware running. No other command was sent.

| Query | Reply |
|---|---|
| `{"o":"get","t":"model"}` | `{"o":"getresult","t":"model","v":"JL_BT"}` |
| `{"o":"get","t":"firmware"}` | `{"o":"getresult","t":"firmware","v":"V0.04"}` |
| `{"o":"get","t":"mac"}` | `{"o":"getresult","t":"mac","v":"20-BE-95-A2-28-A0"}` |
| `{"o":"get","t":"battery"}` | `{"o":"getresult","t":"battery","v":"100"}` |

- The serial protocol works as described: JSON at 460800 baud. The port is silent until queried.
- The MAC equals the Bluetooth address seen over BLE.
- `V0.04` does not identify the variant: all five stock images contain the string `V0.04`.

### Update command on the stock firmware (2026-11-10, with the owner's go-ahead)

- The first command after the serial port is opened gets no reply (also true for `get`). A `JUMPBOOT` sent as the first command did nothing; the app kept answering `get` queries.
- After two warm-up `get` queries, `{"o":"JUMPBOOT"}` took effect at once: the serial port disappeared ("Device not configured") and the device came back as a **USB HID device**: still `4c4a:4155` "USB Composite Device" (Jieli Technology), one interface of class 3 (HID), vendor usage page `0xFF00`, usage 1. No serial port.
- So the boot code does act on the stock app's reset: the last inferred link of the update entry is now observed. This is the device the vendor's `UbootHid.exe` talks to.
- Nothing was sent to the device in update mode.

Leaving update mode (observed, same day):

- After the owner's first re-plug of the cable, the device enumerated again as the HID updater. No serial port.
- After a second re-plug, it came back as the normal firmware: serial port `/dev/cu.usbmodem1301`, and the four `get` queries gave the same replies as before (`JL_BT`, `V0.04`, same MAC, battery 100).
- So a visit to update mode with nothing sent does not change the firmware, and the device leaves update mode without a host tool.
- Unknown: what made the difference between the two re-plugs (time unplugged, a button press, or a timeout in the boot code).
- In this run the first `get` after opening the port was answered. The dropped first command is therefore not constant; keep the warm-up `get`.

Consequence for the custom firmware: a host tool should send a `get` (or any bytes) before `JUMPBOOT`, or send `JUMPBOOT` twice. The custom firmware's own matcher accepts `JUMPBOOT` anywhere in the stream, including as the first bytes.

## The updater's protocol (static analysis, 2026-11-10)

Sources: the vendor's `UbootHid.exe` and the boot code `uboot.boot`, read side by side. Not yet run against the device.

`uboot.boot` is LZ4-compressed: 16-byte header, 16 bytes of fields, then one LZ4 block at offset `0x20`. The dictionary is the whole mask ROM (`0x7000` bytes). It unpacks to `0x2940` bytes that load at RAM `0x2000`.

Packets go in 64-byte HID reports, the same layout both ways:

```
AA 55 | len u16 | cmd | status | payload | crc16
```

`len` = payload + 2. `cmd`..payload are XORed with `4e 61 bc 00` (repeating). The CRC is the image CRC-16, over `AA`..payload after the XOR.

| Cmd | Payload | What the boot code does |
|---|---|---|
| `C0` init | name[16], flag | Selects a region by name: a boot-table entry (`app_dir_head`, ...) or `uboot_zone` (flash `0`..`0x10000`). Replies address, length, base offset, block size. |
| `C2` erase | address, type | Erases; type 1 / 2 / 3 = `0x100` / `0x1000` / `0x10000` bytes (the boot code's table and the vendor tool's assembly agree). |
| `C3` write | address, length, data | Programs up to 41 bytes (vendor tool's chunk). |
| `C4` crc | address, length, block size | Replies one CRC-16 per block. The block size is the device's own, not the packet's. |
| `CA` reboot | none | Writes the "ok" tag to RAM `0x800` and resets. |
| `C1`, `CB` | | Not used by the vendor tool. `CB` writes a header record; do not send it. |

Facts that matter:

- **There is no read command.** A byte-for-byte backup through the updater is not possible.
- **The CRC command covers any region**, so the device can be compared with a known image block by block. If every block matches a vendor image, that image is the backup of the app area.
- **The vendor tool only writes the app area** (from the `app_dir_head` offset). It never touches the boot code.
- **Order of a flash:** erase block 0, write the other changed blocks, write block 0 without its first 32 bytes, then those 32 bytes, then compare all CRCs.
- **Entry into update mode at power-on** (inferred from the boot code): the tag `ubootZst` at RAM `0x800`, or an app-area head whose CRC check fails. So an interrupted flash comes back in update mode. An app with valid CRCs that does not run is not caught by this.

`tools/e2boot.py` implements `info`, `verify`, `flash`, `reboot`, `enter` and `rescue` (wait for the unit, then flash). `flash` first compares the boot area's CRCs with the image and refuses an image whose boot area differs. `tools/test_e2boot.py` tests it against a model of the handlers above.

## Observed with `tools/e2boot.py` (2026-11-10)

Only `enter`, `info`, `verify` (init and CRC commands) and `reboot` were sent. No erase, no write.

- `info`: `app_dir_head` = address `0x3000`, length `0xffffffff`, base offset `0`, block size `0x1000`. `uboot_zone` = address `0`, length `0x10000`, base offset `0`, block size `0x1000`.
- **The unit runs the `zoom-phone-tablet` image.** 41 of its 42 app-area blocks match. The other four images match in 0 blocks.
- **The boot area is the vendor's.** Flash `0`..`0x3000` matches the image, block for block.
- **One block differs:** flash `0x2c000`..`0x2d000`, the last block before the `VM` partition. In the image it holds 3170 bytes that follow `cfg_tool.bin`, then `0xff`. What the unit holds there is unknown and cannot be read. The vendor tool would overwrite this block with the image's on any update.
- **The flash is 256 KB.** CRCs repeat after 64 blocks.
- `reboot` works: the serial port came back within 4 s and the `get` replies were unchanged.

## Restore proof (2026-11-10)

`e2boot.py flash firmware/e2control-zoom-phone-tablet.bin --yes`, with the owner's go-ahead. The first write to the unit.

- Before: 41 of 42 blocks matched. The tool erased and rewrote block 0 and the one differing block (erase type 2, 41-byte writes, head last).
- After: 42 of 42 blocks match. `reboot` brought the stock firmware back: serial port present, `get` replies unchanged (`V0.04`, same MAC).
- So erase, write and the write order work on this unit with `tools/e2boot.py`. The unit's former content of flash `0x2c000`..`0x2d000` is gone.
- Still to check by the owner: Bluetooth pairing and the three buttons on the restored stock firmware.

## First custom flash (2026-11-10)

`e2boot.py flash fw/out/e2-kindle.bin --yes` (SHA-256 `e6459768…cac5faa`, built on the `zoom-phone-tablet` image), with the owner's go-ahead, after the owner confirmed that the restored stock firmware paired and all three buttons worked.

- Items 4 and 5 of the "Before flashing" checklist in `fw/README.md` (pins measured on the board) were open. The owner was told and chose to go ahead.
- 41 of 42 blocks written; 42 of 42 match afterwards.
- After `reboot` the custom firmware enumerates as USB serial `4c4a:4155`.
- **The update path works from the custom firmware:** `e2boot.py enter` (its `JUMPBOOT` handler) put the unit into the HID updater, `verify` matched 42 of 42, `reboot` brought the firmware back.
- Not yet tested: Bluetooth advertising, pairing with the Kindle, the buttons, power-off and wake-up.
- To return to stock: `e2boot.py enter PORT --yes`, then `e2boot.py flash firmware/e2control-zoom-phone-tablet.bin --yes`, then `e2boot.py reboot --yes`.

## Lockout after the first custom flash (2026-11-10, open)

Observed:

- The custom firmware started twice, both times by the updater's `reboot` (a reset, not a power-on). It answered on USB.
- A few minutes later the owner moved the selector to Off and back. Since then the unit has not appeared on USB (no serial port, no HID updater) and does not advertise over Bluetooth (12 s scan from the Mac).
- Tried without effect: USB re-plug, the previous and next buttons, the selector, both page keys held at switch-on.

Inferred cause (not proven on the unit):

- The SDK's `app_main` runs `check_power_on_voltage` before the app starts: 11 readings of the chip's own VBAT below 3.30 V, then soft power-off. That happens before USB and Bluetooth start.
- The stock firmware replaced this check. Its `app_main` (`0x1E0A0B0`) measures the battery on PA8 (`adc_get_voltage(AD_CH_PA8) * 2 / 10`, `0x1E0557A`) and powers off below 2.80 V (`0x118`). The constant 330 does not occur in it. So on this board the chip's VBAT reading is not the battery, or sits near 3.3 V.
- Not explained: why the two starts by reset passed the check.
- Other candidate: the unit is in soft power-off and its wake-up does not work. Wake-up was never tested.

Fix in the sources (not on the unit): the SDK's battery detect is off (`TCFG_SYS_LVD_EN 0`: no VBAT check at start, no low-battery event), self power-off is off (`TCFG_E2_SELF_POWER_OFF 0`) until wake-up is proven, LED on PB5 driven, and a status line on the serial port (send `?`).

Ways back in: a start that passes the check (then `e2boot.py enter PORT --yes` at once, or `make fw-rescue yes=1` waiting for it), or the mask ROM's USB download mode, which needs a forced-download tool.

Forced download, read from this chip's ROM listing (`sdk/rom.lst`, `usb_key_isr` at `0x106350`, `check_key_type` at `0x10563a`):

- The ROM takes a 16-bit key, `0x16EF`, shifted in most significant bit first. A timer captures edges on D+ (`set_timer3_captrue_dp`), so D+ is the clock and D- the data. This agrees with kagaimiq's `jielie/isp/usb/usb-key.md` (clock about 50 kHz, repeated until acknowledged).
- On a match the ROM drives both lines low for 2 ms, then starts its USB download mode (`usb_slave_mode`).
- Not read yet: when after a reset the ROM listens, and for how long. Not tried on the unit.
- After that, a host tool for the ROM loader is needed (`hlyflash` or `jl-uboot-tool`, with the BD19 loader from the SDK). hlyflash states that BD19 is untested.

## Open questions for the device step

- What the unit holds in the one differing block, and whether anything needs it.
- Does ROM download mode start on this unit with a forced-download tool?

## Effect on the plan

Plan Task 3 names `hlyflash` for the backup. That route does not fit this unit, and the updater cannot read flash. The backup becomes: prove with `verify` which vendor image the unit holds, and keep that image. Data outside the app area (pairing data, settings) cannot be saved; the flash does not touch it.
