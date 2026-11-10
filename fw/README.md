# fw: Jieli SDK build

Builds the Kindle Click app (`fw/src` on top of the Jieli AC632N SDK HID tree, `apps/hid/board/bd19`,
board `CONFIG_BOARD_AC632N_DEMO`) in a `linux/amd64` container. Output: `fw/out/app.bin`,
raw q32s code linked for entry `0x1E00120` (first bytes `eeff xxxx xxxx edff`).

    fw/build.sh                       # (or `make fw-build`) clones the SDK into fw/sdk/ (gitignored), builds, writes fw/out/app.bin
    SDK_SRC=/path/to/clone fw/build.sh   # same, cloning from a local copy instead of GitHub

- SDK: `https://github.com/Jieli-Tech/fw-AC63_BT_SDK` at commit `fdc018de81629e21e594d677cf05f87d288be0f0` (HEAD is verified after checkout).
- `fw/sdk` is owned by the script: each run does `git reset --hard` to that commit, `git clean -fdx`, copies
  `src/*.c src/*.h` to `sdk/apps/hid/kindle_click/`, then applies every `patches/*.patch` in name order. Do not keep work in it. Every build is therefore a full rebuild (about 20 s).
- After the container step the script fails unless `app.bin` is 100-240 KB, starts `ee ff .. .. .. .. ed ff`, and the `.text`
  section of `sdk.elf` starts at `0x1e00120`.
- `patches/0001-select-keypage.patch` is applied to the clone: `CONFIG_APP_KEYBOARD 0`, `CONFIG_APP_KEYPAGE 1`.
- `patches/0003-kindle-click-app.patch` switches to the Kindle Click app (next section). `0002` is reserved for the E2 board.
- Reference result of the stock example (unmodified SDK + 0001 only, before 0003 existed): `app.bin` = 228176 bytes, starts `ee ff d0 19 00 00 ed ff`.
  Builds differ in the embedded build-time bytes: the SDK logs `__DATE__`/`__TIME__` (`apps/hid/app_main.c:184`) and
  each `app.bin` contains `Nov 10 2026` plus the `HH:MM:SS` of its build. Three fresh builds (12:45:46, 12:46:00, 12:46:15)
  were compared pairwise with `cmp -l`: 3, 3 and 2 differing bytes, all within offsets 109030-109033 (the time string).
  So the SHA-256 changes on every build and the build is not bit-for-bit reproducible.
- `build.sh` refuses to reset/clean unless `git -C fw/sdk rev-parse --show-toplevel` equals the physical path of `fw/sdk`,
  so a missing or broken `fw/sdk/.git` cannot make it clean the parent project.

## Kindle Click app

`src/app_kindle_click.c` is the app (keys, BLE events, advertising, power; started from `apps/hid/examples/keypage/app_keypage.c`),
`src/app_kindle_send.c` the report path (press queue and 20 ms send tick); `src/kc_*.c` are the SDK-free protocol, key and link
units that `make fw-test` tests on the host. (The SDK-bound files must not be named `kc_*.c`: `make fw-test` compiles that glob.) `0003` changes, all on top of 0001:

| Where (SDK path) | Change |
|---|---|
| `apps/hid/include/app_config.h` | `CONFIG_APP_KEYPAGE 0`, `CONFIG_APP_KINDLE_CLICK 1`; for this app `TCFG_USER_EDR_ENABLE 0` and the EDR profile switches `USER_SUPPORT_PROFILE_HID/PNP 0` (BLE only; the SDK `#error`s if EDR is off with EDR profiles on) |
| `apps/hid/include/app_action.h`, `apps/hid/app_main.c` | `ACTION_KINDLE_CLICK` and the `start_app` branch for it |
| `apps/hid/board/bd19/Makefile` | the four `kindle_click/*.c` files and `-I apps/hid/kindle_click` |
| `apps/hid/config/lib_btstack_config.c` | `config_le_sm_sub_sc_enable = CONFIG_BT_SM_SUPPORT_ENABLE` (no effect on BD19, see below) |
| `apps/common/include/bt_common.h` | new `COMMON_EVENT_BLE_CCCD_WRITE`, posted to the app on every CCCD write |
| `apps/hid/modules/bt/ble_hogp.c` | ADV_IND data = `kc_adv_data` exactly, name only in the scan response; interval 32 x 0.625 ms = 20 ms; `PAIR_RECONNECT_ADV_EN 0` (always undirected ADV_IND; stock sent 2 x 1.28 s of high-duty `ADV_DIRECT_IND` to the stored peer address after boot and after a supervision timeout); Firmware Revision `e2kc-0.1`; SM auth `SM_AUTHREQ_BONDING` only (stock also asked for MITM); Battery Level notifications only on an encrypted link (`sm_encryption_key_size`): right after the peer enables the CCCD, after encryption on a reconnect, and on change; the CCCD-write event; handle references cut down to the new table (one input report, no output report, no `ae40`/`ae30` transfer channels) |
| `apps/hid/modules/bt/ble_hogp_profile.h` | Replaced by the output of `fw/att_table.py gen` (see "ATT table" below) |

Unchanged stock settings that the Kindle needs: `slave_security_auto_req = 0`, `slave_set_wait_security = 0`,
`IO_CAPABILITY_NO_INPUT_NO_OUTPUT` (`hogp_sm_init_config`), `CONFIG_BT_SM_SUPPORT_ENABLE 1`, fixed public address derived from
the BT MAC (`btstack_ble_start_before_init`, `same_address = 0`), Battery Level notify on change (60 s poll, `TCFG_SYS_LVD_EN`).
PnP ID and report map are handed in from the app (`le_hogp_set_PNP_info`, `ble_init_cfg_t.report_map`).

### ATT table

`fw/att_table.py gen` writes `ble_hogp_profile.h` (the table and its handle `#define`s) from a short service list;
`fw/att_table.py dump FILE` walks a table in a `.h` or in a binary (`fw/out/app.bin`, a vendor `app.bin`) and prints handle, UUID,
flags and value. To change the table: edit `SERVICES` in the script, run `gen` into `fw/sdk/.../ble_hogp_profile.h` on a prepared
tree, regenerate 0003, and compare `dump fw/out/app.bin` with `dump` of the header. Contents:

- GAP `1800` (name, appearance, PPCP), GATT `1801` (Service Changed + CCCD), Device Information `180a` (as stock; PnP ID `READ`
  only, stock had `AUTHENTICATION_REQUIRED`, which BD19 `att_db.c` answers with Insufficient Authentication (0x05) unless the
  link has a MITM key, and Just Works never gives one), Battery `180f` (Level read/notify + CCCD).
- HID `1812`: Protocol Mode (`01`), one Input Report (ID 1, Report Reference `01 01`) with its CCCD, Report Map, HID Information,
  HID Control Point. Values and the CCCD need encryption (flags `0x6000`: key size >= 7, the SM minimum); MITM is not required;
  the Report Reference stays readable without encryption, as in the stock and vendor tables.
- Dropped from stock: input reports 2-6, the output report (ID 1, keyboard LEDs), Boot Mouse Input `2a33`, Jieli `ae40`
  (`ae41`/`ae42` transfer channel, only weak log handlers in this app) and `ae30` (`ae01`-`ae05`, `ae10`, the SDK test/data
  channel; `ae10` only fed a weak log handler). `ae00` (RCSP, BLE OTA) is only in the stock table under `RCSP_BTMATE_EN`, which is 0
  (`CONFIG_APP_OTA_ENABLE` off); the generated header `#error`s if RCSP is turned on.

Vendor reference (observed, `dump` of the vendor Kindle build `2387LJ` for AC636N, table at file offset 0x1e547): GAP, Battery,
`ae30` (`ae01` write / `ae02` notify), HID with Information, Report Map, Control Point, Protocol Mode, Input Report ID 1 + CCCD +
`01 01`, the output report `01 02`, input reports 2-6, boot keyboard in/out (`2a22`/`2a32`), boot mouse `2a33`, Device Information
(PnP with `AUTHENTICATION_REQUIRED`), and RCSP `ae00`; no GATT `1801`. None of its HID entries needs encryption. Inferred from
that: the Kindle starts pairing on its own, and the extra reports do not stop it. Not shown by it: whether the Kindle reads
PnP ID successfully on that build (AC636N has a different library) or reacts well to Insufficient Encryption; the encryption
flags here follow the design ("encryption required, MITM not") and standard HOGP behaviour, untested on the Kindle.

Board-dependent values come from the SDK board config (now `board_ac632n_demo_cfg.h`; the E2 board patch replaces it): key numbers
(AD key values), soft-off wake-up pins (`TCFG_E2_WAKE_KEY1/2_PORT`, `TCFG_E2_USB_SENSE_PORT`, see "Power"), low-power mode, LED,
battery measurement, `TCFG_HID_AUTO_SHUTDOWN_TIME` (0, unused).

## USB update entry

The vendor updater can only reflash the E2 if the running app takes it into the boot area's USB HID updater
(`uboot.boot`, kept unchanged by `jlimg.py pack`). This firmware keeps that entry. Nothing below has run on a device.
Addresses are in the zoom-phone-tablet `app.bin` (load `0x1E00120`), Ghidra project `ghidra/project/E2`.

**In this build (demo board config) the key entry is NOT compiled in: the USB command is the only way into update mode.**
`fw/build.sh` prints which on its last line ("E2 update: key entry compiled in" / "NOT compiled in").

Before flashing the custom firmware onto the E2:

1. The build log says "key entry compiled in" for the real board config, and PA0 is not the debug UART TX
   (`TCFG_UART0_TX_PORT`; on the demo config it is, which is why the key entry is off).
2. On the device, still running the STOCK firmware, send `JUMPBOOT` (the vendor updater) and confirm the unit enters its
   USB HID updater. This proves that the boot code acts on the tag written at `0x800`, which is inferred, not read.
3. After the first flash, check first that the custom firmware enumerates as `4c4a:4155` CDC.
4. (iv) The USB-power sense pin `TCFG_E2_USB_SENSE_PORT` and the wake-up key pins `TCFG_E2_WAKE_KEY1_PORT` / `TCFG_E2_WAKE_KEY2_PORT`
   are assumed (PA7, PB7, PA0, from the stock firmware): verify all three against the real board, and that `fw/build.sh`
   lists the wake-up pins you expect ("E2 power: wake-up pins compiled in").
5. (v) On the real board, confirm that the sense pin reads high when the USB cable is plugged in (and low when it is not),
   before relying on it. If it is wrong, the unit either powers off while plugged in or never powers off.

What is built (`patches/0004-usb-update-entry.patch`, `src/e2_update.c`, `src/kc_update_cmd.c`):

- `board_ac632n_demo_cfg.h`: `TCFG_PC_ENABLE 1`, `USB_DEVICE_CLASS_CONFIG (CDC_CLASS)` (CDC-ACM only), `usb_std_class_def.h`
  defaults (CDC endpoints 0x83 interrupt / 0x04 out / 0x84 in), `TCFG_OTG_USB_DEV_EN 0` as stock, and the key pins
  `TCFG_E2_UPDATE_KEY1_PORT IO_PORTB_07`, `TCFG_E2_UPDATE_KEY2_PORT IO_PORTA_00` (stock keys 1 and 2). The board Makefile
  gets the two new sources.
- `e2_update_usb_start()`, called in `app_start` right after `btstack_init()` and `sys_key_event_enable()`, the same place as
  stock: SDK `usb_start()` (CDC device on USB0), then `cdc_set_wakeup_handler(cdc_rx)` in place of the SDK's echo demo.
  `cdc_rx` (USB interrupt) feeds each byte to `kc_update_cmd_feed` and calls `e2_update_enter()` on a match.
- `kc_update_cmd_feed`: true when the 8 bytes `JUMPBOOT` have arrived contiguously anywhere in the stream, also across
  packets (stock: `strstr(packet, "JUMPBOOT")` per packet). One byte of state. Host-tested (`test_update_cmd`).
- `e2_update_enter()`: `memcpy(0x800, "ubootZst", 8)`, `hw_mmu_disable()`, `cpu_reset()` (= `p33_soft_reset`).
- `e2_update_boot_keys()` (first call in `app_start`, before BLE): sets both key pins to pull-up input, and if both read low
  for 3000 ms without a break, `e2_update_enter()`. Compiled in only when both pins are set and neither is the UART0 TX pin.
- Power-off policy and wake-up sources: see "Power" below.

Observed (static):

- Updater (`app.asar`, `electron/service/device.js`, `common/SerialPortManager.js`): opens the first serial port with VID `4C4A`
  PID `4155` at 460800 baud, writes `JSON.stringify({"o":"JUMPBOOT"})` (16 bytes, one write), runs `UbootHid.exe` 3 s later
  (20 tries, JUMPBOOT sent again at try 10). "Connected" means only that the port opened. The UI sends `{"o":"get","t":
  "battery"|"mac"|"firmware"}` every second until it has a battery value, but only shows the replies; the update button does
  not depend on them. So `get` is not answered.
- Stock CDC receive callback (`0x1E02FC2`, set by stock `usb_start` `0x1E05964` through `cdc_set_wakeup_handler`): reads one
  packet (<= 64 bytes); unless it starts with `S` (segment header), copies it to `0x24AC`, runs the JSON handler, then calls
  `FUN_01E02562` if `strstr(packet, "JUMPBOOT")` or if the first byte is `0xAA`.
- `FUN_01E02562`: `memcpy(0x800, 0x84A, 8)` (`.data`, = `ubootZst`), `call 0x1E14822` (`JL_MMU->CON &= ~BIT(0)`), `goto 0x4462`
  (`[0x1A18A0] |= 0x30`). Our SDK libraries: `hw_mmu_disable` (`system.a`, memory.c) and `p33_soft_reset` (`cpu.a`, power_hw.c)
  are exactly these two operations. No SDK routine in this tree writes `ubootZst`; `go_mask_usb_updata` (msd_upgrade.c) is a
  different entry (ROM tag `usb\xedc\x81g\xef` via `nvram_set_boot_state`), so the sequence is written out in `e2_update_enter`.
- RAM `0x800` is where the mask ROM keeps its own reset tags (`nvram_boot_state`/`nvram_set_boot_state`, rom.lst `0x100378`):
  it compares 8 bytes there with its usb/uart/OTA tags and leaves anything else alone. `uboot.boot` contains the strings
  `uboot\xa5ok`, `ubootZst`, `uboot_zone` next to each other (LZ4 sequences at file offset `0x21BC`), as the stock `.data` at
  `0x842`/`0x84A` does.
- Stock USB: device descriptor `12 01 10 01 00 00 00 40 4a 4c 55 41 00 01 01 02 03 01`, IAD + CDC-ACM template, strings
  "Jieli Technology" / "USB Composite Device", config header `09 02 .. 01 00 a0 32`: byte-identical to ours. Stock
  `cdc_desc_config` (`0x1E01696`) patches in EP `0x83`/`0x04`/`0x84` like ours; `cdc_hdl` is 0x74 bytes (no interrupt-EP mutex,
  `CDC_INTR_EP_ENABLE 0`). Serial number: both use the flash UUID (`get_iserialnumber_str`).
- Stock start-up (BT app, `0x1E05B72`-`0x1E05B8E`): `btstack_init` (`FUN_01E0DE82`), `sys_timeout_add(FUN_01E060FA, 180000)`,
  `sys_key_event_enable` (`FUN_01E16002`), `usb_start` (`FUN_01E05964`). A second `usb_start` (`0x1E0A17A`) runs instead of
  BT when `JL_PORTA->IN` bit 7 (PA7) is high at start, followed by an `idle` app.
- Stock `otg_data` (RAM `0x858`) = `00 02 02 02 03 02 32` (`usb_dev_en = 0`), ours identical. Stock `usb_otg_init`
  (`0x1E086F6`) = ours: a port whose `usb_dev_en` bit is clear gets state 7, and the detector timer is only added when
  `usb_dev_en != 0`. Stock `usb_dev_idle_query` (`0x1E080CC`, in its `.lp_target` list as "otg") = ours (`0x1E0523A` in our
  build): busy if a port is in a state > 2. The low-power gate (`__power_get_timeout`, port_overwrite.c:451, ours `0x1E10C08`) ANDs all
  `is_idle` results, so in both firmwares the chip never powers down.
- Stock power-down hook: `sleep_enter_callback` `0x1E073C2` -> `close_gpio` `0x1E0720A` (clears `0x1E1800` bit 0 = USB0 IO
  mode, like ours `0x1E02F30`); `board_set_soft_poweroff` `0x1E073EC` -> same `close_gpio`. Registered by `0x1E094B6`, as ours
  (`power_set_callback`). Since the chip never powers down, `close_gpio` runs only at soft power-off.
- Stock auto power-off: the 180 s timeout above (`FUN_01E060FA`: BT shut down (inferred), then `sys_timeout_add(FUN_01E05470, 300)`, which writes
  P11 registers and loops: power-off, inferred) is re-armed by `sys_timer_modify` to 180 s while no link exists and to 30 min
  (`0x1B7740`) while one does (link = connection handles `0x2E4E`/`0x2E50` non-zero, inferred) (`0x1E063FC`, `0x1E06394`, `0x1E04156`); it is deleted while PA7 is high (`FUN_01E0558C`). A 15 s timer
  (`0x1E054B6`) also powers off after 12 ticks without a connection.
- Stock keys: AD key on PA1 (`adkey_data` `0x1E18EF4`, values 0, 4, 5, 6) and IO keys (`iokey_list` `0x1E18E8A`): key 1 = PB7,
  key 2 = PA0, key 3 = PA2, all to GND, with a combination remap BIT(1)|BIT(2) -> key 7 (`MULT_KEY_ENABLE`).

Inferred:

- uboot enters its HID updater when it finds `ubootZst` at `0x800` after the reset, and RAM `0x800` survives the soft reset
  (the ROM's own tags rely on that). Not read in uboot code. Checklist item 2 settles it.
- PA7 high means USB power is present (stock then drops BT and cancels the power-off timer).

Unknown / not reproduced:

- Which events re-arm stock's 30-min connected power-off (key presses or only BT events): not established. Ours: key events
  only, by owner decision (see "Power").
- Stock's PA7 behaviour (BT off while USB power is present): not reproduced; ours keeps BLE running and only holds off the
  power-off (see "Power").
- Not reproduced from stock, on purpose: the `0xAA`-first-byte trigger, the `get`/`set` protocol, and stock `setup_arch`'s
  clearing of a `uboot\xa5ok` tag at `0x800` (`0x1E090D8`; it runs after `boot_info_init` `0x1E17E02` has rewritten byte
  `0x800`, so it rarely matches; effect unknown).

## Power

Like stock, the chip never enters power-down while it runs (the USB device is up), so it draws its full idle current for as
long as it is on. The unit therefore switches itself off (soft power-off). Policy in `src/e2_power.c` (a 1 s poll; the
decision is the pure `kc_power_should_off` in `src/kc_power.c`, host-tested by `test_power`):

- **Powers off:** 180 s after advertising starts with no connection; 30 min after the last key event while connected (every
  key event re-arms it; it is dropped on disconnect and the 180 s rule takes over when advertising restarts). The SDK's
  low-battery event (`POWER_EVENT_POWER_LOW`) also powers off, with or without USB power.
- **Keeps it on:** USB power present, i.e. the sense pin `TCFG_E2_USB_SENSE_PORT` reads high (active high, plain GPIO input with
  internal pull-down), at boot and at any time after. A power-off that would be due re-arms instead: the 180 s / 30 min counts
  restart when the cable is unplugged. BLE keeps running regardless of the pin.
- **Wakes it (from soft power-off):** a press of either page key (`TCFG_E2_WAKE_KEY1_PORT`, `TCFG_E2_WAKE_KEY2_PORT`, falling
  edge, pull-up) and plugging in USB power (rising edge on the sense pin). Wake-up table: `board_ac632n_demo.c` `wk_param`,
  patched by `0004`. A key whose pin is the debug UART TX (`TCFG_UART0_TX_PORT`; PA0 in the demo config) is left out, as in
  `e2_update.c`; the build log says which pins are in ("E2 power: wake-up pins compiled in").
- **Press again after a wake-up:** the key press that wakes the unit is not sent as a page turn (no wake-key detection): the
  first press wakes the remote, press again to turn the page. Owner decision.

Pin assignments (`board_ac632n_demo_cfg.h`, all **assumed, to be verified on the board**, see "Before flashing" 4 and 5):
sense `IO_PORTA_07` (stock reads PA7 at start, see Observed), keys `IO_PORTB_07` and `IO_PORTA_00` (stock keys 1 and 2).

Not verified: which pull the prebuilt power library applies to a wake-up pin with `pullup_down_enable` (expected: pull-up for
the falling-edge keys, pull-down for the rising-edge sense pin; `close_gpio` leaves the three pins alone at soft power-off).
Check that the unit stays off with no key pressed and no cable, and wakes on each of the three.

Not covered: a USB cable plugged in during the 300 ms between the decision to power off and the power-off itself gives no
rising edge, so the unit stays off until the next key press or replug.

## BD19 library capabilities

What the prebuilt BD19 Bluetooth libraries can do for the Kindle. The `.a` members are LLVM bitcode; symbols were listed with
`nm --plugin LLVMgold.so` and the code read as IR with `llvm-link -S` (both in the toolchain image:
`/opt/jieli/q32s/bin/nm`, `/opt/jieli/common/bin/llvm-link`). "Observed" = seen in that code; "inferred" = not tested on a device.

**LE Secure Connections: not available.**
- Observed: `sm_set_authentication_requirements` in `btstack.a(sm.c.o)` tests bit `0x08` (`SM_AUTHREQ_SECURE_CONNECTION`); if set it prints
  `<ble-error> :fail select SC,err= %d!!!` and stores `auth_req & ~0x08`. `le_gatt_common.c` ORs that bit in when
  `config_le_sm_sub_sc_enable` is 1, and it is removed again here.
- Observed: `sm.c.o` references no P-256/ECDH code (no `uECC_*`, no `ll_hci_*p256*`/`*dhkey*`); its only crypto calls are
  `ll_hci_encryption` (AES) and `ll_hci_get_le_rand`. P-256 code exists only for BLE mesh (`btstack.a(mesh_hci.c.o)`) and BR/EDR
  (`btctrler.a(lmp.c.o)`, `authentication.c.o`).
- Observed: `sm.c` reads `config_le_sm_sub_sc_enable` in one place only, together with `config_le_sm_sub_sc_bridge_edr_enable`, to call
  `spp_set_miss_connect_en(0)` on disconnect.
- Inferred: pairing with the Kindle will be LE legacy Just Works (STK from TK = 0), bonded. Whether the Kindle accepts legacy pairing is
  the open question from the design ("If Secure Connections is missing, test whether the Kindle accepts legacy Just Works pairing").

**Resolving the Kindle's resolvable private address with its IRK: built in, no switch.**
- Observed: there is no privacy/IRK switch in `lib_btstack_config.c` or `lib_btctrler_config.c`.
- Observed: when bonding, the responder asks for and offers `ENC | ID | SIGN` key distribution (`(auth_req & 1) | 6` in `sm.c`), so
  the peer's IRK is received (`SM_CODE_IDENTITY_INFORMATION recieve`). `sm.c` then calls `ble_list_pair_add` (Jieli's bond list,
  `btstack.a(remote_device_list.c.o)`, kept in VM by `syscfg_write`). Inferred, not traced argument by argument: the IRK is among
  what `ble_list_pair_add` stores, since `check_random_addr` below reads an IRK from the list entry.
- Observed: the bond list matches a connecting address with `check_random_addr`: if the top two address bits are `01` (resolvable
  private address) it computes `ah(IRK, prand)` with `irk_enc` (`btctrler.a(ll_resolve_list.c.o)`) and compares the 3-byte hash; otherwise
  it compares the address. `sm.c` uses this list on reconnect (`ble_list_reconnect_device_is_exist` when the LTK is requested,
  `ble_list_get_conn_addr_item`).
- Observed: BTstack's own resolver in `sm.c` (`sm_address_resolution_*`) iterates `le_device_db`, but `le_device_db_memory.c` on BD19 is a stub
  (`le_device_db_count` returns 0, `le_device_db_add` returns -1), so that path never matches.
- Observed: the controller has an LL privacy resolving list (`ll_resolving_list_add`, `le_privacy`, ...), gated by `LL_PRIVACY` in
  `config_btctler_le_features`, which the SDK leaves off. It is not enabled here: an undirected ADV_IND with no filter policy does not need it.
- Inferred: a bonded Kindle reconnecting from a new resolvable private address is recognised and its LTK found. Not tested.

**Other facts used here**
- `sys_timer_get_ms()` (`system.a(timer.c.o)`) is `jiffies * jiffies_unit` (`jiffies_unit = 10`), so it counts in 10 ms steps and wraps
  modulo 2^32 consistently; `jiffies_msec()` masks `jiffies` to 27 bits and would break the `kc_link` subtraction at the wrap.
- ATT permissions (`att_db.c`, `att_validate_security`, observed): `AUTHENTICATION_REQUIRED` (`0x0400`) needs `att_connection.authenticated`
  (set from `sm_authenticated`, error 0x05 otherwise); the top 4 flag bits are the required key size - 1 (error 0x0F if the link is
  not encrypted, 0x0C if the key is shorter).
- Sending with `ATT_OP_AUTO_READ_CCC` (`btstack.a(att_send.c)`, `user_data_att_send`, observed): reads the CCC of `att_handle + 1`
  and, if notify/indicate is not enabled, returns -93 (`GATT_CMD_USE_CCC_FAIL`) without queuing anything. So the table must keep
  each CCCD right after its value (the generator does), and the report path treats -93 as "not subscribed" and keeps the press
  until the next CCCD write. Inferred: `ble_user_cmd_prepare` passes that value back unchanged (le_gatt_common.c decodes it with
  the same error table).
- Clearing the bond uses the SDK's own sequence from `app_mouse.c`: `ble_module_enable(0)`, `le_hogp_set_pair_allow()` (hogp pair info and
  `ble_list_clear_all()`, both in VM), `ble_module_enable(1)`.

## Toolchain

Jieli's download page (`http://pkgman.jieliapp.com/doc/all`, linked from the SDK Makefile header) redirects to:

| What | URL | SHA-256 |
|---|---|---|
| Linux toolchain `jieli-linux-toolchains-20260730.1` (26027508 B), holds `q32s/`, `common/`, `pi32/`, `pi32v2/`; clang 4.0.1, x86-64 | `https://jl-update.oss-cn-shenzhen.aliyuncs.com/jieli-linux-toolchains-20260730.1.tar.xz` (short link `https://pkgman.jieliapp.com/s/linux-toolchain`) | `a1545cddbc451a06bac43eb5cd96d831470c42a97c5c0b1f98879a20fc7647bd` |
| Linux post-build tools `jieli-linux-post-build-tools-20260923.1` (38662628 B): `isd_download`, `fw_add`, `ufw_maker`, `packres`, ... | `https://jl-update.oss-cn-shenzhen.aliyuncs.com/jieli-linux-post-build-tools-20260923.1.tar.xz` (short link `https://pkgman.jieliapp.com/s/linux-postbuild`) | `6949d48c4cd82e274d7943d4ca67f214593fdb4f699ef9f51c2ecc78125ab739` |

The `Dockerfile` downloads both inside the image build, checks the hashes and unpacks them under
`/opt/jieli` (toolchain) and `/opt/jieli/post-build` (packers). Nothing from these archives runs on the host.
The short links are "latest" pointers, so a future toolchain release will change the hash; the pinned OSS URLs stay.

## Why build.sh does not call the SDK's `make` end to end

`make` compiles and links fine (`cpu/bd19/tools/sdk.elf`). Its Linux post-build script
(`cpu/bd19/tools/download.sh`, generated from `download.c`) then breaks:
it runs from the wrong directory, `lz4_packet` is stored non-executable, and its tail needs
`/opt/utils/strip-ini` and `host-client` (Jieli's hosted packer), which are not in the public archives.
`build.sh` therefore runs `make pre_build`, links `sdk.elf`, `chmod +x lz4_packet`, and runs the first half of
`download.sh` from `cpu/bd19/tools` (objcopy sections, `lz4_packet`, `cat ... > app.bin`).

## Does the SDK packer run on Linux, and does it take a chip key?

Packer runs: yes. `isd_download` (v4.2.81, x86-64 ELF, a Qt program) runs in the container. It needs Qt runtime libraries
that the image does not install (add them if you want to pack): `libsm6 libice6 libx11-6 libxext6 libfontconfig1 libglib2.0-0 libgl1
libxkbcommon0 libxkbcommon-x11-0 libdbus-1-3 libxcb1 libxcb-xinerama0 libfreetype6 libxrender1 libgbm1 libegl1 libxcb-cursor0
libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-shape0 libxcb-xkb1 libxcb-randr0 libxcb-xfixes0 libxcb-sync1
libxcb-shm0 libpulse0 libasound2 libnss3 libxcomposite1 libxdamage1 libxrandr2 libxi6 libxtst6 libxkbfile1 libwayland-client0
libdrm2 libpng16-16 libharfbuzz0b libicu72 libpcre2-16-0 libdouble-conversion3 libmd4c0 libglx0 libopengl0` and `QT_QPA_PLATFORM=offscreen`.
With the arguments from the SDK's `download/data_trans/download.bat`
(`isd_download isd_config.ini -tonorflash -dev bd19 -boot 0x2000 -div8 -wait 300 -uboot uboot.boot -app app.bin cfg_tool.bin -res p11_code.bin -uboot_compress -flash-params flash_params.bin`,
run in a directory holding `app.bin`, `isd_config.ini` (from `isd_config_rule.c`), `uboot.boot`, `cfg_tool.bin`, `p11_code.bin`,
`flash_params.bin`, `ota.bin`) it prints "Device offline, only package the file", writes `jl_isd.bin` (241664 B, a flash image) and
`jl_isd.fw`, and prints the flash layout (`ENTRY_ADDR 0x1e00120`). It exits 245 with "no valid ota upgrade found" /
OTA "minimal space size" FAIL lines, because the demo layout leaves too little flash for the stock OTA images.

Chip key: partly. `isd_download` has a `-key` option (`ERR_INVALIDARG` without a value). It rejects every value I tried
(`1234`, `0x1234`, `ffff`, 8/16/32 hex digits, a 4-byte file) with `Invalid key text` / `Invalid key file`, so the accepted
key syntax is unknown. The SDK's own flow passes no key. Task 2 does not depend on the packer.
