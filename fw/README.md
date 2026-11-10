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
(AD key values), wakeup pin (`port0` = the AD key pin), low-power mode, LED, battery measurement, `TCFG_HID_AUTO_SHUTDOWN_TIME`.

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
