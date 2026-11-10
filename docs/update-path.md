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

## Open questions for the device step

- (Answered: the unit answers `get` queries, see above.) Does `JUMPBOOT` behave as described? What does the device look like on USB after the reset (VID/PID, interface)?
- Can the boot-area HID updater **read** flash back? If not, a full backup needs ROM download mode.
- Which stock variant does the unit run, and is its `uboot.boot` the same as in the vendor images?
- Does ROM download mode start on this unit with a forced-download tool?

## Effect on the plan

Plan Task 3 names `hlyflash probe --enter-boot` and `hlyflash dump` for the backup. That route does not fit this unit. The backup route must be chosen first: the boot-area updater, if it can read flash, or ROM download mode with a forced-download tool.
