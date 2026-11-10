# Kindle Click–Compatible E2 Control Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Own firmware for the HANLINYUE E2 Control (Jieli AC632N) that the owner's Kindle Colorsoft (5.20.1.0.2, stock) accepts as a Kindle Click page turner.

**Architecture:** A small overlay on Jieli's public SDK HID app (`apps/hid`, BD19). All Kindle-specific logic (advertising bytes, report map, reports, key mapping, link-ready gate) is plain C with no SDK includes, tested on the host; one glue file connects it to the SDK's `ble_hogp`. The image for the E2 is made by replacing only `app.bin` and `cfg_tool.bin` inside the unit's own flash backup, so the bootloader and per-unit data stay byte-identical.

**Tech Stack:** C (Jieli q32s clang toolchain in a Linux container; host `cc` for tests), Python 3 stdlib (`tools/jlimg.py`), `hlyflash` for USB flash access, Ghidra project already in this repo for board facts.

**Spec:** `docs/superpowers/specs/2026-11-09-kindle-click-firmware-design.md`. Read it first. It holds the protocol facts, the owner's decisions and the reasons.

## Global Constraints

- No write to the E2 before Task 5 (restore proof) is done. Never flash an image built for another chip (the vendor Kindle builds are AC636N).
- Every step marked **[DEVICE WRITE]** needs the owner's explicit go-ahead at that moment. Steps marked **[OWNER]** need the owner's hands (cable, case, Kindle).
- Every purchase is proposed to the owner first.
- SDK: `Jieli-Tech/fw-AC63_BT_SDK` at commit `fdc018d`, fetched into `fw/sdk/` (gitignored), never copied into git.
- Chip key `0x284c`. App load address `0x1E00120`. Flash 256 KB.
- BLE only; EDR off. The remote never sends a security request.
- Amazon IDs: manufacturer data `71 01 04 43`, PnP ID `01 71 01 43 04 00 01`. Never advertise UUID `0xFE03`.
- Firmware images, flash backups and vendor `.ufw` files stay out of git. Record SHA-256 values in git.
- Makefile targets follow the existing style: `## [STEP N] …` comments, `fw-` prefix.
- Commit after each task. Branch: `firmware-re` (or a branch from it).

## Review Focus

1. **Press before the link is ready** (first ~2.5 s after connect): the press is held and sent once, not lost and not doubled. → Task 8 tests.
2. **Many presses while reconnecting:** the queue is bounded (4); extra presses are dropped, oldest kept, no crash. → Task 8 tests.
3. **Long press on key 3 must not also act as a click**, and a hold must not repeat Sleep. → Task 7 tests.
4. **New app larger than the app area** (would overwrite `VM`): the packer refuses and writes nothing. → Task 2 tests.
5. **The Kindle forgot the remote** (owner removed it under Accessories) while the remote still holds a bond: the remote must still be pairable after the clear-pairing gesture. → Task 11 acceptance step.

---

## File Structure

| Path | Responsibility |
|---|---|
| `tools/jlimg.py` | existing unpacker; gains `pack` (Task 2) |
| `tools/test_jlimg.py` | asserts for unpack/pack round trip |
| `fw/Dockerfile`, `fw/build.sh` | toolchain container; fetch SDK at pinned commit, apply overlay, build |
| `fw/src/kc_proto.h`, `fw/src/kc_proto.c` | constants and byte tables: adv data, report map, PnP ID, report builders. No SDK includes. |
| `fw/src/kc_keys.h`, `fw/src/kc_keys.c` | key event → action. No SDK includes. |
| `fw/src/kc_link.h`, `fw/src/kc_link.c` | link-ready gate and press queue. No SDK includes. |
| `fw/src/app_kindle_click.c` | glue to SDK: BLE setup, key events, battery, LED, power, advertising timeout |
| `fw/board/board_e2control.c`, `fw/board/board_e2control_cfg.h` | pins and board config from Task 6 |
| `fw/tests/test_kc.c` | host tests for the three `kc_*` units |
| `tools/blecheck/` | macOS BLE checker (moved from the session's `gatt.swift` probe), extended in Task 9 |
| `docs/board.md`, `docs/update-path.md` | findings of Tasks 4 and 6 |

---

### Task 1: Toolchain and unmodified SDK build

**Files:** Create `fw/Dockerfile`, `fw/build.sh`; modify `Makefile`, `.gitignore` (`fw/sdk/`, `fw/out/`).

**Interfaces:** Produces `make fw-build` → `fw/out/app.bin` (q32s code for `0x1E00120`) and, if the SDK packer runs, `fw/out/jl_isd.bin`.

- [ ] **Step 1:** Find the Linux q32s toolchain. The SDK `Makefile` header and `README` name the download; the SDK expects `/opt/jieli/q32s/bin/clang`. Record the URL and SHA-256 of the archive in `fw/README.md`. **Gate:** if no Linux toolchain can be downloaded, stop and report; the fallback is a Windows VM with `C:/JL/pi32` and it changes this task only.
- [ ] **Step 2:** Write `fw/Dockerfile` (Debian, toolchain under `/opt/jieli`) and `fw/build.sh` (clone SDK at `fdc018d` into `fw/sdk/` if absent; build `apps/hid/board/bd19`).
- [ ] **Step 3:** In the SDK config, select `CONFIG_APP_KEYPAGE 1` (and `CONFIG_APP_KEYBOARD 0`) in `apps/hid/include/app_config.h`, board `CONFIG_BOARD_AC632N_DEMO`. Apply through a patch file `fw/patches/0001-select-keypage.patch`, not by editing the clone by hand.
- [ ] **Step 4:** Add `fw-build` to the `Makefile`. Run `make fw-build`. Expected: exit 0 and `fw/out/app.bin` exists, size between 100 KB and 240 KB.
- [ ] **Step 5:** Check the output is real q32s code: the first two instructions of every stock `app.bin` are `mov sp,#imm32 ; mov ssp,#imm32`, bytes `ee ff .. .. .. .. ed ff`. Run `xxd -l 8 fw/out/app.bin`. Expected: `eeff xxxx xxxx edff`.
- [ ] **Step 6:** Find out whether the SDK packer (`cpu/bd19/tools/download/…`, `isd_download`) runs on Linux and whether it accepts a chip key. Write the answer in `fw/README.md`. Either answer is fine: Task 2 does not depend on the SDK packer.
- [ ] **Step 7:** Commit.

### Task 2: Image packer (`jlimg.py pack`)

**Files:** Modify `tools/jlimg.py`; create `tools/test_jlimg.py`; modify `Makefile` (`fw-image` target), and commit the pending boot-table change already in the working tree.

**Interfaces:**
- Produces: `pack(base_image: bytes, app_bin: bytes, cfg_tool: bytes | None, key: int) -> bytes` and CLI `jlimg.py pack BASE_IMAGE APP_BIN OUT [--cfg CFG_TOOL] [--key HEX]`. `cfg_tool=None` keeps the base image's `cfg_tool.bin`.
- Rules: output length equals `len(base_image)`; bytes before `app_base` are identical to the base (header, boot table, `uboot.boot`, `isd_config.ini`); `app_area_head`, `app.bin` and `cfg_tool.bin` records get new offset, size and CRC-16; `cfg_tool.bin` follows `app.bin` directly; unused app-area bytes are `0xFF` before scrambling; everything at or after the `VM` partition offset is copied from the base unchanged.

- [ ] **Step 1: Write the failing tests** in `tools/test_jlimg.py` (plain `assert`, run by `python3 -I tools/test_jlimg.py`):

```python
import pathlib, sys, tempfile
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import jlimg
IMG = pathlib.Path(__file__).parent.parent / "firmware" / "e2control-zoom-phone-tablet.bin"

def parts(image):
    with tempfile.TemporaryDirectory() as d:
        jlimg.unpack(image, d)
        return (pathlib.Path(d, "app.bin").read_bytes(), pathlib.Path(d, "cfg_tool.bin").read_bytes())

def test_roundtrip_is_byte_identical():
    base = IMG.read_bytes(); app, cfg = parts(base)
    assert jlimg.pack(base, app, cfg, 0x284C) == base

def test_new_app_unpacks_and_keeps_boot_area():
    base = IMG.read_bytes(); app, cfg = parts(base)
    new_app = app[:0x1000] + bytes(len(app) - 0x1000 - 64)          # shorter, different
    out = jlimg.pack(base, new_app, None, 0x284C)
    assert len(out) == len(base) and out[:0x3000] == base[:0x3000]
    assert out[0x3000 + 0x2D000:] == base[0x3000 + 0x2D000:]         # VM, BTIF, key_mac untouched
    got_app, got_cfg = parts(out)                                    # unpack re-checks every CRC
    assert got_app == new_app and got_cfg == cfg

def test_app_too_large_is_refused():
    base = IMG.read_bytes()
    try:
        jlimg.pack(base, bytes(0x2D000), None, 0x284C)               # reaches the VM partition
    except ValueError as e:
        assert "VM" in str(e)
    else:
        raise AssertionError("oversized app accepted")

if __name__ == "__main__":
    for n, f in list(globals().items()):
        if n.startswith("test_"): f(); print("ok", n)
```

- [ ] **Step 2:** Run `python3 -I tools/test_jlimg.py`. Expected: fails with `AttributeError: module 'jlimg' has no attribute 'pack'`.
- [ ] **Step 3:** Implement `pack` and the `pack` CLI in `tools/jlimg.py`, reusing `enc`, `crc16`, `record`, `table`. The record layout is the `struct` format already in `record()`: `<HHIIBBH16s` (record CRC, data CRC, offset, size, attr, reserved, last, name); the record CRC covers bytes 2–31. `app_area_head.size` is `app.bin` size + `cfg_tool.bin` size + the table (compare with `layout.json` of the stock images to confirm before trusting it).
- [ ] **Step 4:** Run the tests. Expected: three `ok` lines.
- [ ] **Step 5:** Run the round-trip on all five images: `for f in firmware/*.bin; do …; done` inside the test or by hand. Expected: byte-identical for all five (kobo has a different `VM` offset; the code must read it from the table, not use `0x2D000`).
- [ ] **Step 6:** Add `fw-image` to the `Makefile`: `jlimg.py pack $(base) fw/out/app.bin fw/out/e2-kindle.bin`, `base=` required. Commit.

### Task 3: Read the device and back up its flash **[OWNER]**

No device writes to flash. `hlyflash info` and boot entry do send commands to the device; get the owner's go-ahead for each.

**Files:** Create `docs/update-path.md` (started here), `backup/` (gitignored), `firmware/BACKUP.sha256`.

- [ ] **Step 1:** Install `hlyflash` in a venv outside the repo. Run `hlyflash doctor`. Expected: no errors.
- [ ] **Step 2:** With the E2 on USB (`/dev/cu.usbmodem*`, `4c4a:4155`): `hlyflash info`. It speaks JSON at 460800 baud (`{"o":"get","t":…}`); an earlier probe at 115200 with AT commands got silence, which is expected. Record model, firmware version, MAC, battery in `docs/update-path.md`. **Gate:** if `info` fails, capture what the vendor's `UbootHid.exe` sends (static analysis of the unpacked updater in the vendor installer) before going on.
- [ ] **Step 3:** Enter download mode: `hlyflash probe --enter-boot`. Record what changed on USB (`ioreg`/`system_profiler SPUSBDataType`): new VID/PID, mass-storage device, `UBOOT1.00` string. hlyflash bundles only the BR25 loader; for this chip pass `--loader fw/sdk/cpu/bd19/tools/bd19loader.bin`. **Gate:** if hlyflash cannot drive BD19, try `kagaimiq/jl-uboot-tool`; if neither works, stop and report.
- [ ] **Step 4:** `hlyflash dump --out backup/e2-stock.bin` (it reads twice and compares). Expected: 262144 bytes. Run a second independent dump to `backup/e2-stock-2.bin`; `cmp` must report no difference.
- [ ] **Step 5:** `python3 -I tools/jlimg.py backup/e2-stock.bin backup/x`. Expected: all CRCs pass, chip key `0x284c`. Compare `backup/x/app.bin` by SHA-256 with `extracted/*/app.bin` and write which stock variant the unit runs into `docs/update-path.md` and the README.
- [ ] **Step 6:** Write the SHA-256 of `backup/e2-stock.bin` to `firmware/BACKUP.sha256`. Ask the owner to copy the backup to a second place outside the repo. Commit docs and the hash.

### Task 4: How download mode is entered, and what survives a bad flash

**Files:** Modify `docs/update-path.md`.

**Interfaces:** Produces the answer Task 10 builds on: the exact bytes that make the stock firmware enter download mode, the stock function that handles them, and the SDK call it ends in.

- [ ] **Step 1:** From Task 3, write down the host→device bytes that trigger download mode (read them from hlyflash's source for `--enter-boot`, or from the capture).
- [ ] **Step 2:** In the Ghidra project of the unit's variant, find the USB CDC receive handler and the branch for that command (start from the strings `"o"`, `"get"`, `"t"` and from references to the ROM stubs in `sdk/maskrom_stubs.ld`; `ghidra/scripts/Xrefs.java` lists references). Record function addresses and the final call (expected: an SDK update/jump-to-uboot routine).
- [ ] **Step 3:** Decide and record: **firmware-assisted** (the app must cooperate) or **ROM-level** (works with any app). Evidence for ROM-level would be: entry still works with the app area erased. Do not erase to find out. If static analysis cannot settle it, treat it as firmware-assisted.
- [ ] **Step 4:** Research a forced-download tool for AC632N (Jieli "强制下载工具"/USB updater dongle) and an AC632N/AC6321A development board. Write model names, prices and sources into `docs/update-path.md` and **propose the purchase to the owner**. Recommended: buy both.
- [ ] **Step 5:** Commit.

### Task 5: Restore proof **[DEVICE WRITE] [OWNER]**

- [ ] **Step 1:** Get the owner's go-ahead for a full-flash write of the unit's own backup.
- [ ] **Step 2:** Write `backup/e2-stock.bin` back with the tool that made the dump (for hlyflash this is its restore/flash path; read its `--help` and `HARDWARE_TEST_REPORT.md` first, and use its dry-run mode before the real write).
- [ ] **Step 3:** Dump again to `backup/e2-after-restore.bin`. Expected: `cmp` with `backup/e2-stock.bin` reports no difference (the `VM` partition may differ if the firmware ran in between; if so, the difference must lie only inside `VM`).
- [ ] **Step 4:** Power-cycle. The owner confirms the stock firmware behaves as before (BLE name `E2 Control` visible from the Mac, a button sends a report).
- [ ] **Step 5:** Record the result in `docs/update-path.md`. Commit.

### Task 6: Board facts **[OWNER]**

**Files:** Create `docs/board.md`, `fw/board/board_e2control.c`, `fw/board/board_e2control_cfg.h`, `fw/patches/0002-board-e2control.patch`.

**Interfaces:** Produces the board config Task 9 compiles against, with SDK key numbers 1, 2, 3 (and 7 if it is a real key) as in stock.

- [ ] **Step 1 [OWNER]:** Open the case; photograph both PCB sides. Read the chip marking (expect AC6321A/AC6323A/AC632N family), locate buttons, LED(s), battery connector, any UART/test pads. Write these into `docs/board.md` with the photos stored outside git or under `docs/img/`.
- [ ] **Step 2:** With the stock firmware and `tools/blecheck` (or the Mac's HID input), press each button (click and long press) and record which report each sends. With Task 3's variant known, this fixes: physical button → SDK key number, and which page button is "previous" and which is "next". Write the table into `docs/board.md`. If inconclusive: key 1 = previous, key 2 = next.
- [ ] **Step 3:** In the decompiled stock firmware, find the key driver setup (IO key vs AD key), LED pin writes, battery ADC and the power-key/latch handling; compare with `apps/hid/board/bd19/board_ac632n_demo.c` and `board_ac632n_demo_cfg.h`, which the vendor started from. Cross-check each pin against the PCB (continuity test where the owner agrees). Record pin, polarity, ADC thresholds.
- [ ] **Step 4:** Write `board_e2control.c/_cfg.h` as copies of the `ac632n_demo` pair with only the recorded differences changed; EDR off (`TCFG_USER_EDR_ENABLE 0`), BLE on. Wire it in with `0002-board-e2control.patch`.
- [ ] **Step 5:** `make fw-build`. Expected: exit 0. Commit.

### Task 7: Protocol tables and key mapping (host-tested)

**Files:** Create `fw/src/kc_proto.h`, `fw/src/kc_proto.c`, `fw/src/kc_keys.h`, `fw/src/kc_keys.c`, `fw/tests/test_kc.c`; modify `Makefile` (`fw-test`: `cc -Wall -Werror -Ifw/src fw/src/kc_*.c fw/tests/test_kc.c -o fw/out/test_kc && fw/out/test_kc`).

**Interfaces (produced):**

```c
/* kc_proto.h */
#define KC_REPORT_ID   1
#define KC_REPORT_LEN  10            /* without the report ID: mod, rsvd, key[6], consumer lo, hi */
typedef enum { KC_NONE = 0, KC_PREV_PAGE, KC_NEXT_PAGE, KC_SLEEP, KC_CLEAR_BOND } kc_action_t;
extern const uint8_t  kc_adv_data[];      extern const uint8_t  kc_adv_data_len;
extern const uint8_t  kc_report_map[];    extern const uint16_t kc_report_map_len;
extern const uint8_t  kc_pnp_id[7];
void kc_report_press(kc_action_t a, uint8_t out[KC_REPORT_LEN]);   /* all zero for actions with no report */
void kc_report_release(uint8_t out[KC_REPORT_LEN]);

/* kc_keys.h — events are the SDK's: 0 click, 1 long, 2 hold, 3 up, 4 double click */
#define KC_CLEAR_BOND_HOLDS 183        /* same count as stock */
kc_action_t kc_key_action(uint8_t key, uint8_t event, uint16_t hold_count);
```

- [ ] **Step 1: Write the failing tests** in `fw/tests/test_kc.c`:

```c
static void test_adv(void) {
    static const uint8_t want[] = { 0x02,0x01,0x06,  0x05,0xFF,0x71,0x01,0x04,0x43,  0x03,0x03,0x12,0x18 };
    assert(kc_adv_data_len == sizeof want && !memcmp(kc_adv_data, want, sizeof want));
    for (int i = 0; i + 1 < kc_adv_data_len; i++)                 /* never 0xFE03 */
        assert(!(kc_adv_data[i] == 0x03 && kc_adv_data[i + 1] == 0xFE));
}
static void test_pnp(void) {
    static const uint8_t want[7] = { 0x01, 0x71,0x01, 0x43,0x04, 0x00,0x01 };
    assert(!memcmp(kc_pnp_id, want, 7));
}
static void test_reports(void) {
    uint8_t r[KC_REPORT_LEN];
    kc_report_press(KC_NEXT_PAGE, r);  assert(r[2] == 0x4E && r[8] == 0 && r[9] == 0);
    kc_report_press(KC_PREV_PAGE, r);  assert(r[2] == 0x4B && r[3] == 0);
    kc_report_press(KC_SLEEP, r);      assert(r[2] == 0 && r[8] == 0x30 && r[9] == 0x00);
    kc_report_press(KC_CLEAR_BOND, r); for (int i = 0; i < KC_REPORT_LEN; i++) assert(r[i] == 0);
    kc_report_release(r);              for (int i = 0; i < KC_REPORT_LEN; i++) assert(r[i] == 0);
}
static void test_report_map(void) {   /* one application collection, report ID 1, 10 bytes */
    int collections = 0;
    for (int i = 0; i + 1 < kc_report_map_len; i++)
        if (kc_report_map[i] == 0xA1 && kc_report_map[i + 1] == 0x01) collections++;
    assert(collections == 1);
    assert(kc_report_map[0] == 0x05 && kc_report_map[1] == 0x01 && kc_report_map[2] == 0x09 && kc_report_map[3] == 0x06);
}
static void test_keys(void) {
    assert(kc_key_action(1, 0, 0) == KC_PREV_PAGE);
    assert(kc_key_action(2, 0, 0) == KC_NEXT_PAGE);
    assert(kc_key_action(3, 1, 0) == KC_SLEEP);
    assert(kc_key_action(3, 0, 0) == KC_NONE);                    /* click on key 3: nothing */
    assert(kc_key_action(3, 3, 0) == KC_NONE);                    /* release after long press: no click */
    assert(kc_key_action(3, 2, 5) == KC_NONE);                    /* hold repeats: no second Sleep */
    assert(kc_key_action(3, 2, KC_CLEAR_BOND_HOLDS) == KC_CLEAR_BOND);
    assert(kc_key_action(3, 2, KC_CLEAR_BOND_HOLDS + 1) == KC_NONE);
    assert(kc_key_action(1, 1, 0) == KC_NONE && kc_key_action(1, 4, 0) == KC_NONE && kc_key_action(9, 0, 0) == KC_NONE);
}
```

- [ ] **Step 2:** `make fw-test`. Expected: compile error, `kc_proto.h` not found.
- [ ] **Step 3:** Implement `kc_proto.c` and `kc_keys.c`. The report map: take the bytes of the vendor's map from `2387LJ` `app.bin` at `0x1E1DC3C` (unpack with `tools/jlimg.py`, print with `tools/hiddesc.py`) and compare with the map in the verified protocol document (spec, Sources). Use the protocol document's map; note any difference from the vendor's in a comment. If Task 6 found the page directions reversed, swap keys 1 and 2 here and in the test.
- [ ] **Step 4:** `make fw-test`. Expected: exit 0.
- [ ] **Step 5:** Commit.

### Task 8: Link-ready gate and press queue (host-tested)

**Files:** Create `fw/src/kc_link.h`, `fw/src/kc_link.c`; modify `fw/tests/test_kc.c`.

**Interfaces (produced):**

```c
#define KC_READY_AFTER_CCCD_MS  800
#define KC_READY_AFTER_ENC_MS   5000
#define KC_QUEUE_LEN            4
typedef struct { /* private */ } kc_link_t;
void        kc_link_reset(kc_link_t *l);                         /* at boot and on clear-bond: not ready, queue emptied */
void        kc_link_on_disconnect(kc_link_t *l);                 /* not ready; queued presses are kept */
void        kc_link_on_encrypted(kc_link_t *l, uint32_t now_ms);
void        kc_link_on_cccd_write(kc_link_t *l, uint32_t now_ms);
bool        kc_link_ready(const kc_link_t *l, uint32_t now_ms);
bool        kc_link_push(kc_link_t *l, kc_action_t a);           /* false when full; the new press is dropped */
kc_action_t kc_link_pop(kc_link_t *l, uint32_t now_ms);          /* KC_NONE unless ready and non-empty */
```

`kc_link_reset` empties everything (boot, clear-bond). `kc_link_on_disconnect` keeps the queue, so the press that woke the remote is delivered after the reconnect (owner decision in the spec). Time is a wrapping `uint32_t`; compare with subtraction.

- [ ] **Step 1: Write the failing tests:**

```c
static void test_link(void) {
    kc_link_t l; kc_link_reset(&l);
    assert(kc_link_push(&l, KC_NEXT_PAGE));
    assert(kc_link_pop(&l, 0) == KC_NONE);                        /* not connected */
    kc_link_on_encrypted(&l, 1000);
    assert(!kc_link_ready(&l, 5999) && kc_link_ready(&l, 6000));  /* 5 s fallback */
    kc_link_on_cccd_write(&l, 1500);
    kc_link_on_cccd_write(&l, 1700);                              /* last CCCD write counts */
    assert(!kc_link_ready(&l, 2499) && kc_link_ready(&l, 2500));
    assert(kc_link_pop(&l, 2500) == KC_NEXT_PAGE);                /* held press sent once */
    assert(kc_link_pop(&l, 2501) == KC_NONE);
    for (int i = 0; i < KC_QUEUE_LEN; i++) assert(kc_link_push(&l, KC_PREV_PAGE));
    assert(!kc_link_push(&l, KC_NEXT_PAGE));                      /* bounded */
    kc_link_on_disconnect(&l);
    assert(!kc_link_ready(&l, 99999) && kc_link_pop(&l, 99999) == KC_NONE);
    kc_link_on_encrypted(&l, 0xFFFFFF00u);                        /* timer wrap */
    kc_link_on_cccd_write(&l, 0xFFFFFF00u);
    assert(kc_link_ready(&l, 0xFFFFFF00u + 800u));
    assert(kc_link_pop(&l, 0xFFFFFF00u + 800u) == KC_PREV_PAGE);  /* kept across the disconnect */
}
```

- [ ] **Step 2:** `make fw-test`. Expected: compile error, `kc_link.h` not found.
- [ ] **Step 3:** Implement `kc_link.c`.
- [ ] **Step 4:** `make fw-test`. Expected: exit 0.
- [ ] **Step 5:** Commit.

### Task 9: SDK glue and bench check with a BLE scanner

Runs on the development board if the owner bought one (Task 4); otherwise build only and verify in Task 11.

**Files:** Create `fw/src/app_kindle_click.c`, `fw/patches/0003-kindle-click-app.patch`, `tools/blecheck/` (move `gatt.swift` and `Info.plist` from the session scratchpad; build steps are in project memory: signed `.app`, launched with `open`; match services `180A`/`180F`, since macOS hides `1812`).

**Interfaces:** Consumes everything in `kc_proto.h`, `kc_keys.h`, `kc_link.h`, and the board config of Task 6.

- [ ] **Step 1:** Start `app_kindle_click.c` from `apps/hid/examples/keypage/app_keypage.c` and the profile in `apps/hid/modules/bt/ble_hogp.c`. Set, each through the patch file:
  - advertising data = `kc_adv_data` in the ADV_IND; device name only in the scan response; interval 20 ms; fixed address;
  - report map = `kc_report_map`; one input report, ID `KC_REPORT_ID`; PnP ID = `kc_pnp_id`; Firmware Revision string `e2kc-0.1`;
  - security: `CONFIG_BT_SM_SUPPORT_ENABLE 1`, `config_le_sm_sub_sc_enable = 1` (`apps/hid/config/lib_btstack_config.c`), IO capability NoInputNoOutput, bonding, `slave_security_auto_req = 0`, `slave_set_wait_security = 0` (`hogp_sm_init_config` in `ble_hogp.c`);
  - privacy: enable resolving of the peer's resolvable private address with the stored IRK (find the SDK switch in `lib_btstack_config.c`; record it in `fw/README.md`);
  - Battery service: read and notify; send a notification after the CCCD is written and when the level changes.
- [ ] **Step 2:** Key path: SDK key event → `kc_key_action` → `kc_link_push`; a 20 ms timer calls `kc_link_pop` and sends `kc_report_press` then `kc_report_release`. `KC_CLEAR_BOND` deletes the bond, calls `kc_link_reset`, restarts advertising. Call `kc_link_on_encrypted`, `kc_link_on_cccd_write`, `kc_link_on_disconnect` from the matching BLE events.
- [ ] **Step 3:** Advertising timeout: stop advertising 180 s after it started without a connection and enter the SDK's low-power state; any key restarts advertising and its action goes through `kc_link_push`. Power on/off, LED and auto power-off: keep what `app_keypage.c` does, on the Task 6 pins.
- [ ] **Step 4:** `make fw-build && make fw-test`. Expected: both exit 0.
- [ ] **Step 5 (board):** Flash the development board (its own full image from the SDK packer or `jlimg.py pack` on the board's backup). Extend `tools/blecheck` to scan and print the advertising payload, then connect and print PnP ID and Firmware Revision. Expected output contains manufacturer data `71 01 04 43`, flags `06`, service `1812`, PnP `01 71 01 43 04 00 01`, revision `e2kc-0.1`.
- [ ] **Step 6:** Commit.

### Task 10: Keep the update path

Skip the code in this task only if Task 4 recorded **ROM-level** entry with evidence. Otherwise this must work before the E2 is flashed.

**Files:** Modify `fw/src/app_kindle_click.c` (or a new `fw/src/e2_update.c` if it exceeds ~80 lines), `fw/board/board_e2control_cfg.h`, `docs/update-path.md`.

- [ ] **Step 1:** Enable the USB CDC device in the board config so the firmware enumerates as stock does (`4c4a:4155`).
- [ ] **Step 2:** Implement only the command from Task 4 that enters download mode, ending in the same SDK call the stock firmware uses. Also answer the `get` fields `hlyflash info` needs before it will proceed (Task 3 shows which); nothing else of the vendor protocol.
- [ ] **Step 3 (board):** On the development board: with the custom firmware running, `hlyflash probe --enter-boot`, then dump. Expected: download mode is entered and the dump matches what was flashed. Repeat three times, including once straight after power-on and once while a BLE connection is up.
- [ ] **Step 4:** Add a second way in that does not need USB parsing to work: holding keys 1 and 2 at power-on calls the same routine. Test it the same way.
- [ ] **Step 5:** Record results in `docs/update-path.md`. Commit.

### Task 11: Pair with the Colorsoft, then flash the E2 **[OWNER]**

- [ ] **Step 1 (board, if available):** On the Colorsoft: Settings › Accessories, pair. **Gate (spec, unverified assumption 2):** if pairing fails, read the SDK debug log on the UART pin; if LE Secure Connections is the cause, set `config_le_sm_sub_sc_enable = 0` and retry with legacy Just Works. If the Kindle never shows the device, compare the advertising payload with a known-good clone's before changing anything else. Record what worked in `fw/README.md`.
- [ ] **Step 2 (board):** Run the acceptance list below on the board. Fix and repeat until it passes.
- [ ] **Step 3:** Build the E2 image: `make fw-image base=backup/e2-stock.bin`. Verify: `python3 -I tools/jlimg.py fw/out/e2-kindle.bin fw/out/x` passes all CRCs; `cmp -n 12288 fw/out/e2-kindle.bin backup/e2-stock.bin` reports no difference (boot area identical); record the SHA-256.
- [ ] **Step 4 [DEVICE WRITE]:** With the owner's go-ahead, flash `fw/out/e2-kindle.bin` to the E2, then read back and compare outside `VM`.
- [ ] **Step 5 [DEVICE WRITE]:** Prove the second flash: from the custom firmware, enter download mode (Task 10, both ways) and flash the same image again.
- [ ] **Step 6 [OWNER]: Acceptance on the E2 with the Colorsoft** (spec, "Done means"):
  - appears under Settings › Accessories and pairs;
  - page buttons turn pages forward and back in the stock directions;
  - a press within 1 s of reconnecting turns exactly one page;
  - five fast presses while disconnected turn at most four pages after the reconnect and nothing hangs;
  - long press on key 3 puts the Kindle to sleep once; releasing does nothing more;
  - wake the Kindle, press a page button: reconnects without re-pairing, and that press turns the page;
  - the Kindle shows a battery level;
  - remove the remote under Accessories on the Kindle, do the clear-pairing hold on the remote, pair again from scratch;
  - leave the Kindle asleep for 10 minutes: the remote has stopped advertising (check with `tools/blecheck` scan), and a press brings it back.
- [ ] **Step 7:** Update the README (new section: Kindle firmware, build and flash commands, backup hash) and project memory. Commit.

---

## Task order and parallel work

- No device needed: Tasks 1, 2, 7, 8 (independent of each other; can run in parallel).
- Owner at the device: 3 → 4 → 5, then 6.
- Then 9 → 10 → 11.
