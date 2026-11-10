# E2 Control: Kindle Click–compatible firmware — design

Date: 2026-11-09. Status: approved in conversation; this document is the reference for the plan.

## Intent

The owner has one HANLINYUE E2 Control page turner (Jieli AC632N) and one Kindle Colorsoft on software 5.20.1.0.2. The Kindle does not list the E2 Control at all. The owner wants their own firmware for the E2 Control that pairs with the Colorsoft and turns pages. A vendor Kindle build for the E2 would be welcome if it appears, but this firmware gets built either way.

**Why it fails today.** Since 5.20.1, stock Kindles (2024 models onward) accept exactly one kind of page turner: a BLE HID device that identifies itself as Amazon's Kindle Click (VID 0x0171, PID 0x0443). Every other BLE HID device is rejected before it reaches the UI. The E2's stock firmware is a generic BLE HID device (stylus taps and consumer keys), so it is ignored.

## Goals

1. The Colorsoft lists the E2 under Settings › Accessories and pairs with it. The Kindle is stock: no jailbreak.
2. The two page buttons turn pages forward and back, in the same directions as the stock firmware on this unit.
3. A long press on the third button puts the Kindle to sleep.
4. The remote reconnects without re-pairing after the Kindle wakes up or the remote powers on. A button press on the remote to start the reconnect is fine.
5. The Kindle shows the remote's battery level.

## Non-goals

- Any other host: phone, tablet, Kobo, Android readers, PC. All stock modes (taps, swipes, zoom, media keys, mode switching) are dropped.
- Brightness keys. The Kindle supports them; the owner did not ask for them. Leave room to add them, do not build them.
- Recreating the vendor firmware's source. Reverse engineering is used only for board facts.
- The vendor's USB configuration protocol and the `AE30` GATT channel. One exception: the USB command that enters update mode, if recovery depends on it (see Owner decisions).
- Waking the Kindle from the remote. The Kindle does not allow it.

## Anti-goals

- Flashing anything before a full flash backup exists and a restore of that backup has been proven on the device.
- Flashing an image built for another chip (the vendor's Kindle builds are AC636N).

Bricking the remote is an accepted risk of the project, but only after the backup/restore step has been done properly.

## What the Kindle requires

Sources: community reverse engineering of Kindle 5.20.1, verified on hardware by open-source clones (links at the end). Treat the verified protocol document as the authority where sources differ.

**Advertising**
- Flags `0x06` (LE general discoverable, BR/EDR not supported) in the ADV_IND itself.
- First manufacturer-data field: `71 01 04 43`, in the ADV_IND, not only the scan response.
- 16-bit service UUID `0x1812`.
- Never advertise UUID `0xFE03`.
- Fixed address (public or static random). A 20 ms advertising interval gives the fastest reconnects.

**GATT**
- HID service: Report Map, one Input Report with report ID 1 (Report Reference `01 01`) and its CCCD. Encryption required, MITM not.
- Device Information: PnP ID `01 71 01 43 04 vv vv`. The vendor's Kindle builds use `01 71 01 43 04 00 01`; use the same. Firmware Revision is displayed by the Kindle.
- Battery service: Battery Level readable and notifying. The Kindle takes the level from notifications only.

**Report map and reports**
- Keyboard and Consumer usages in one Application Collection, report ID 1.
- 10-byte report: modifiers, reserved, six key slots, 16-bit consumer usage.
- Next page: keyboard `0x4E` (Page Down). Previous page: keyboard `0x4B` (Page Up). Sleep: consumer `0x0030` (Power). Each press is a report with the usage, then an all-zero report.
- (Not built: brightness up/down are consumer `0x006F` / `0x0070`.)

**Pairing and connection**
- LE Secure Connections, Just Works (NoInputNoOutput), bonding.
- The Kindle drops security requests from the remote. The remote must not request security; the Kindle starts pairing and encryption.
- The Kindle reconnects from a resolvable private address, so the remote must keep the Kindle's IRK and resolve it.
- Every connection triggers a full GATT rediscovery. Keys sent too early are lost. Treat the link as ready 800 ms after the last CCCD write, or 5 s after encryption as a fallback. A press before that is held and sent once the link is ready, not dropped.
- The Kindle turns Bluetooth off about 100 s after it sleeps. The remote must go back to advertising and reconnect later.

## Behavior of the remote

| Input | Action |
|---|---|
| Page button A, click | Previous page (keyboard `0x4B`) |
| Page button B, click | Next page (keyboard `0x4E`) |
| Third button, long press | Sleep (consumer `0x0030`) |
| Long hold (builder picks the button and time, close to stock) | Clear the bond and advertise for a new pairing |

- Which physical button is A and which is B: same directions as the stock firmware on this unit. In the stock zoom build, SDK key 1 is "previous" and key 2 is "next". The unit's variant is unknown, so the plan confirms the directions on the device (see Board facts) before the mapping is final. If the test is inconclusive, use key 1 = previous, key 2 = next; swapping later is a one-line change.
- Third button, click: no action (builder's choice; nothing was requested).
- Power on/off, LED behavior and idle power-down: keep close to stock as far as the board facts allow. Exact LED patterns are left to the builder.
- Device name: builder's choice. The Kindle identifies the remote by IDs, not by name. *(My call.)*

## Unverified assumptions

These carry the most risk. The plan must test each one early, before work that depends on it.

1. **The Kindle side.** The protocol facts above come from third-party documents, gathered by web research for this design. Nobody has checked them against this Colorsoft. The owner confirmed that the Colorsoft shows Settings › Accessories with a page-turner pairing option, so the feature is enabled on this model. The protocol details stay unverified until the first pairing test. Stronger check, optional: pair one of the existing open-source clones (ESP32 or nRF52 board) if the owner has one.
2. **The AC632N BLE library.** The vendor did this on AC636N, a different chip family with a different prebuilt Bluetooth library. On AC632N (BD19) it is unverified that the SDK library supports LE Secure Connections, resolving the Kindle's private address from a stored IRK, and suppressing the peripheral's security request (the SDK HID example sends one by default). If Secure Connections is missing, test whether the Kindle accepts legacy Just Works pairing before giving up.
3. **Recovery after a bad flash.** See Flashing and safety: a restore that works from the stock firmware does not prove a restore works from broken firmware.
4. **Button directions.** "Key 1 = previous" is read from swipe directions in the stock zoom build, and the unit may run another variant.

## Owner decisions on hardware, recovery and power

- **Hardware work is allowed.** The owner agreed to opening the case, soldering, and buying extra hardware. So the plan may: read button, LED and battery pins from the PCB; use a UART pin for debug logs; buy a Jieli forced-download tool; buy an AC632N development board. Preferred route: develop and test the Kindle profile on a development board against the Colorsoft, and flash the E2 only when the profile works. What to buy is left to the builder, but each purchase is proposed to the owner first.
- **The update path stays.** If download mode on this unit depends on the running firmware, the new firmware keeps the vendor's USB path into update mode. That part of the vendor USB protocol is then in scope (only the command that enters update mode, nothing else), and it is the first feature built and tested. A forced-download tool is a second line of defense, not a replacement.
- **A button press may wake the remote.** After the Kindle has slept, the owner presses a button; the remote reconnects and then delivers that press. The remote does not advertise forever. Goal 4 is read this way: no re-pairing and no steps on the Kindle, but a button press on the remote is fine.

### Later decisions (2026-11-10)

These replace the hardware points above where they differ.

- **No development board.** The owner wants to develop and test on the E2 Control itself. The pairing test, the update-path test and the bench check all happen on the E2.
- **No forced-download tool.** The owner chose to go ahead without one and accepts that a bad first custom flash could be final. The backup therefore depends on what the boot-area updater can do; if it cannot read flash, there is no full backup, and the fallback baseline is the vendor's stock image for the unit's variant (per-unit data in `VM`/`BTIF` is then not backed up, and an app-only flash does not touch it).
- **Order:** board facts (open the case) come before the first custom flash, because the firmware must use the E2's real pins. The first custom flash is followed at once by a second flash from the custom firmware, to prove the update path.
- **Pairing risk:** the BD19 Bluetooth library has no LE Secure Connections (found in plan Task 9). The firmware offers legacy Just Works pairing. Whether the Kindle accepts it is unknown until the first pairing attempt on the E2.

## How it gets built

All of this section is my call; the owner handed these choices over.

- **Base:** Jieli's public SDK, `Jieli-Tech/fw-AC63_BT_SDK` (Apache-2.0; commit `fdc018d` is the one already used in this repo), starting from `apps/hid` for the BD19/AC632N boards. The stock firmware was built from this example: its Device Information strings match `apps/hid/modules/bt/ble_hogp.c`. BLE only; classic Bluetooth (EDR) off.
- **Reference:** the vendor's Kindle builds for AC636N (`2387LJ.ufw` E3 Control, `8F5FLJ.ufw` Free3, `3943LJ.ufw` Free2 2026, from `cdn.hanlinyue.com.cn`). They unpack with `tools/jlimg.py` (chip key `0x284c`) and contain the PnP ID above and a keyboard+consumer report map at report ID 1 (in `2387LJ` at `0x1E1DC3C`). Use them to check advertising data, descriptor and timing choices. Never flash them.
- **Toolchain:** Jieli's q32s clang toolchain. The SDK Makefile expects it at `/opt/jieli/q32s/bin` on Linux, or `C:/JL/pi32` on Windows. The owner works on macOS, so the default is a Linux container. Whether the Linux toolchain can still be downloaded is unverified; checking it is the first step of the plan. If it cannot, fall back to a Windows VM. The SDK's image-packing tools (`isd_download`, `fw_add`, `ufw_maker`) may be Windows-only; check and handle the same way.
- **Chip key:** the packed image must be scrambled with chip key `0x284c`, as all vendor images for this unit are. How to give the SDK's packer this key (it normally uses a vendor `.key` file) is an open item; if the packer cannot, `tools/jlimg.py`'s scrambler can be extended to write images.
- **Bootloader:** keep the unit's own `uboot.boot` from the backup unless there is a proven reason to replace it with the SDK's. The preferred first flash changes only the app area (`app.bin`, `cfg_tool.bin`), with the boot table and `uboot.boot` byte-identical to the backup.
- **Where the code lives:** in this repo, as a small overlay on the SDK (board config, HID profile, key handling), with the SDK fetched at the pinned commit, not copied in. Layout is left to the builder.

## Board facts still to find

The stock images are decompiled in Ghidra (`make ghidra-import`; see the README). Known so far, from the zoom build: button dispatcher `FUN_01e05e46(event, key)`, key numbers 1, 2, 3 and 7 in use, SDK key events (click, long, hold, up, double-click).

To find before the firmware is final:
- Which GPIOs, or which ADC channel and thresholds, the buttons use, and how key numbers map to physical buttons.
- LED pins and polarity.
- Battery measurement (ADC channel, divider) and charge detection.
- Power latch / power key handling.
- Flash size and partition layout to keep (`layout.json` from `tools/jlimg.py`; the stock images use a 256 KB layout with `VM` and `BTIF` partitions).

Sources for these: the PCB itself (the owner agreed to open the case), the decompiled stock firmware compared with the SDK's `board_ac632n_demo.c` and its config header, and observation of the device (BLE reports per button with the stock firmware).

## Flashing and safety

- **Backup first.** Dump the full flash over USB with the device in Jieli's UBOOT download mode (tools: `osteotek/hlyflash`, `kagaimiq/jl-uboot-tool`). Then prove restore: write the dump back and confirm the stock firmware still works. Only then flash new firmware. The dump also tells us which stock variant the unit runs.
- **Entering download mode** on this unit is not yet known (the USB port normally shows only a CDC serial device, `4c4a:4155`). The vendor's Windows updater (`UbootHid.exe`) does it; `hlyflash` documents the method for sibling models. Finding it is part of the plan.
- **Two kinds of download mode.** Entry requested through the running stock firmware stops working if we flash firmware that does not boot or does not implement it. Unbricking then needs entry forced through the mask ROM at power-up, which on Jieli chips normally needs a special USB signal (a "forced download" tool or equivalent). The plan must find out which kind this unit offers. If entry is firmware-assisted, the new firmware keeps the stock USB path into update mode (owner's decision, see above).
- **The restore test is the first write.** Writing the dump back is itself a risk; it needs the same go-ahead as any other write.
- **OTA as an alternative.** The vendor flashes its other models over BLE with Jieli's "杰理OTA升级" app and `.ufw` files. Whether the E2's stock firmware accepts OTA is unknown. It may be used if it proves safer; it does not replace the USB backup.
- Every step that writes to the device needs the owner's explicit go-ahead at the time. The owner has accepted the risk that the remote ends up unusable.
- The device has per-unit data (BT address, `BTIF` partition). The new image must not destroy what the backup cannot restore; the full-flash backup covers this.

## Order of work

1. Build tools: get the toolchain and packer running; build the unmodified SDK HID example for AC632N.
2. Backup: enter download mode, dump the flash, prove restore.
3. Board facts: pins, LED, battery, button directions.
4. Firmware: Kindle Click profile, key handling, pairing/reconnect behavior, battery.
5. Flash and test with the Colorsoft.

Steps 1 and 3 can run in parallel; step 1 does not touch the device. With a development board, most of step 4 happens on the board before step 5 touches the E2. Step 2 can start as soon as the owner is at the device.

## Done means

On the owner's Colorsoft (5.20.1.0.2), with stock Kindle software:
- The remote appears under Settings › Accessories and pairs.
- Page buttons turn pages forward and back, in the stock directions, including the first press after a reconnect.
- A long press on the third button puts the Kindle to sleep.
- After the Kindle wakes, a button press reconnects the remote without re-pairing, and that press turns the page.
- A second flash of the E2 is possible after the first custom image: the update path was exercised from the custom firmware, not only from stock.
- The Kindle shows a battery level.
- The clear-pairing gesture works: after it, the Kindle can pair the remote again from scratch.
- The remote does not drain its battery while the Kindle sleeps: advertising stops after a timeout and resumes on a button press, and that press is delivered after the reconnect.
- The flash backup of the stock firmware is kept outside git, with its SHA-256 recorded.

## Scope note

The firmware identifies itself with Amazon's vendor and product IDs so that the Kindle accepts it. This is for the owner's own device. Distributing images is out of scope.

## Left to the builder

Repo layout for the firmware, container details, LED patterns, timings not fixed above, the clear-pairing gesture, device name, firmware revision string, and how to test without the Kindle (for example a BLE scanner checking the advertising data and GATT table against this document).

## Sources

- Kindle Click protocol, verified on hardware: https://github.com/patricksuo/zectrix-note4-esp32-kindle-page-turner/blob/main/docs/PROTOCOL.md
- Kindle 5.20.1 static analysis: https://github.com/zuyan9/Open-Kindle-Click-nRF52840/blob/main/PROTOCOL.md
- Working clones: https://github.com/zuyan9/Open-Kindle-Click-ESP32-C6 , https://github.com/zuyan9/Open-Kindle-Click-nRF52840
- Known Kindle-side issues: https://github.com/patricksuo/zectrix-note4-esp32-kindle-page-turner/blob/main/docs/KNOWN_ISSUES.md
- Jieli SDK: https://github.com/Jieli-Tech/fw-AC63_BT_SDK
- Vendor firmware pages: https://hanlinyue.com.cn/1782274267396 , https://hanlinyue.com.cn/e3-control-quan-jian-rong-ban-gu-jian-xia-zai
- Flash tools: https://github.com/osteotek/hlyflash , https://github.com/kagaimiq/jl-uboot-tool
- This repo's README: device identity, image format, memory map, stock button behavior.
