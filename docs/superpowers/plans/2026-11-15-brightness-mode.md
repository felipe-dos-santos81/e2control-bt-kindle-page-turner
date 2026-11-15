# Brightness Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A short press on the E2's third button switches between page mode and brightness mode; in brightness mode next and previous change the Kindle's screen brightness.

**Architecture:** The mode lives in the pure, host-tested key logic (`kc_keys`), which gets the current time from its caller. Two new actions in `kc_proto` carry the brightness keys through the existing press queue and send path. The SDK-bound app file passes the time, shows the mode on the LED and resets the mode when the link changes.

**Tech Stack:** C99 for `fw/src/kc_*` (host tests with `make fw-test`), Jieli AC63 SDK glue in `fw/src/app_kindle_click.c` (built with `make fw-build` in Docker), `tools/e2boot.py` to flash.

**Spec:** `docs/superpowers/specs/2026-11-15-brightness-mode-design.md`

## Global Constraints

- Brightness keys are consumer usages `0x006F` (up) and `0x0070` (down), in bytes 8 and 9 of input report 1, low byte first.
- Return to page mode `15000` ms after the last button event in brightness mode, when the link drops, and after a long press on the third button.
- Next is brighter, previous is dimmer. Key numbers: previous `1`, next `2`, third button `3`.
- Do not change `kc_adv_data`, `kc_report_map`, `kc_pnp_id` or any pairing setting.
- `fw/src/kc_*.c/.h` stay pure C99 with no SDK includes. They compile with `-std=c99 -Wall -Wextra -Werror`.
- Never edit `fw/sdk/` by hand. This plan needs no SDK patch.
- Nothing may block or power the unit off before `e2_update_usb_start()` runs (`AGENTS.md`).
- Every write to the device needs the owner's go-ahead at that moment. `tools/e2boot.py enter`, `flash` and `reboot` need `--yes`.
- Commit only when the owner asks. Tasks end with the work uncommitted.
- Docs use plain, short sentences and separate observed, inferred and unknown.

## Review Focus

1. The millisecond clock wraps (32 bits, about 49 days): brightness mode must still end 15 s after the last event, and not end early. Test in Task 2.
2. The first press after the 15 s have run out: it must turn a page, not change the brightness. Test in Task 2.
3. Releasing a held page button in brightness mode: the release must not add a step. Test in Task 2.
4. A long press on the third button while in brightness mode: the Kindle sleeps, the mode ends, and the count toward "forget the pairing" is not disturbed. Test in Task 2.
5. A new link must start in page mode, so a page press made while the Kindle reconnects is a page turn. Reset on connect and on disconnect in Task 3; check on the unit in Task 4.

---

### Task 1: Brightness actions and their report bytes

**Files:**
- Modify: `fw/src/kc_proto.h` (the `kc_action_t` enum)
- Modify: `fw/src/kc_proto.c` (`kc_report_press`)
- Test: `fw/tests/test_kc.c` (`test_reports`)

**Interfaces:**
- Produces: `KC_BRIGHT_UP` and `KC_BRIGHT_DOWN`, added at the end of `kc_action_t` (after `KC_CLEAR_BOND`, so existing values keep their numbers). `kc_report_press(KC_BRIGHT_UP, out)` and `kc_report_press(KC_BRIGHT_DOWN, out)` fill the 10-byte report.

- [ ] **Step 1: Write the failing test.** Add to `test_reports` in `fw/tests/test_kc.c`:

```c
    kc_report_press(KC_BRIGHT_UP, r);   assert(r[2] == 0 && r[8] == 0x6F && r[9] == 0x00);
    kc_report_press(KC_BRIGHT_DOWN, r); assert(r[2] == 0 && r[8] == 0x70 && r[9] == 0x00);
```

- [ ] **Step 2: Run it.** `make fw-test`. Expected: compile error, `KC_BRIGHT_UP` undeclared.
- [ ] **Step 3: Add the two enum values and the two `case` lines** in `kc_report_press`, next to `KC_SLEEP`. Update the comment above the enum's users if it lists the actions.
- [ ] **Step 4: Run it.** `make fw-test`. Expected: `ok test_reports` and every other `ok` line, exit 0.

### Task 2: The mode in the key logic

**Files:**
- Modify: `fw/src/kc_keys.h`, `fw/src/kc_keys.c`
- Test: `fw/tests/test_kc.c` (new `test_brightness_mode`, new `test_brightness_mode_ends`, existing `test_key_event`)

**Interfaces:**
- Consumes: `KC_BRIGHT_UP`, `KC_BRIGHT_DOWN` from Task 1.
- Produces, in `kc_keys.h`:

```c
#define KC_BRIGHTNESS_IDLE_MS 15000u    /* brightness mode ends this long after the last key event */

typedef struct {
    uint16_t aux_holds;       /* consecutive KC_EV_HOLD events on KC_KEY_AUX */
    uint8_t  brightness;      /* 1 in brightness mode */
    uint32_t last_event_ms;   /* time of the last key event in brightness mode */
} kc_keys_t;

/* now_ms: a millisecond clock that may wrap. */
kc_action_t kc_key_event(kc_keys_t *st, uint8_t key, uint8_t event, uint32_t now_ms, uint8_t *times);
/* 1 while in brightness mode at now_ms. Applies the KC_BRIGHTNESS_IDLE_MS return. */
bool kc_keys_brightness(kc_keys_t *st, uint32_t now_ms);
/* Back to page mode: the link changed. */
void kc_keys_page_mode(kc_keys_t *st);
```

- `kc_key_action` and `kc_key_repeat` keep their signatures and stay the page-mode mapping.
- Rules for `kc_key_event`, on top of today's behavior:
  - First apply the idle return: brightness mode ends when `now_ms - last_event_ms >= KC_BRIGHTNESS_IDLE_MS` (unsigned subtraction).
  - Third button, `KC_EV_CLICK`: toggle the mode, return `KC_NONE` with `*times == 0`. Multi-click events on the third button stay `KC_NONE` and do not toggle.
  - Third button, `KC_EV_LONG`: `KC_SLEEP` as today, and page mode.
  - In brightness mode, previous gives `KC_BRIGHT_DOWN` and next gives `KC_BRIGHT_UP`: click and multi-click with the count from `kc_key_repeat`; `KC_EV_LONG` and every `KC_EV_HOLD` with `*times == 1`; `KC_EV_UP` gives `KC_NONE`.
  - Every event handled while in brightness mode sets `last_event_ms = now_ms`.

- [ ] **Step 1: Write the failing tests.** In `test_key_event`, add `0` as the new `now_ms` argument to every existing `kc_key_event` call. Add these two functions and call them from `main` with their `puts("ok ...")` lines:

```c
static void test_brightness_mode(void) {
    kc_keys_t k = {0}; uint8_t t;
    assert(!kc_keys_brightness(&k, 0));                                             /* starts in page mode */
    assert(kc_key_event(&k, KC_KEY_AUX, KC_EV_CLICK, 1000, &t) == KC_NONE && t == 0);
    assert(kc_keys_brightness(&k, 1000));
    assert(kc_key_event(&k, KC_KEY_NEXT, KC_EV_CLICK, 1100, &t) == KC_BRIGHT_UP && t == 1);
    assert(kc_key_event(&k, KC_KEY_PREV, KC_EV_CLICK, 1200, &t) == KC_BRIGHT_DOWN && t == 1);
    assert(kc_key_event(&k, KC_KEY_NEXT, KC_EV_TRIPLE_CLICK, 1300, &t) == KC_BRIGHT_UP && t == 3);
    assert(kc_key_event(&k, KC_KEY_PREV, KC_EV_LONG, 2000, &t) == KC_BRIGHT_DOWN && t == 1);   /* held */
    assert(kc_key_event(&k, KC_KEY_PREV, KC_EV_HOLD, 2150, &t) == KC_BRIGHT_DOWN && t == 1);
    assert(kc_key_event(&k, KC_KEY_PREV, KC_EV_UP, 2300, &t) == KC_NONE && t == 0);            /* release: no step */
    assert(kc_key_event(&k, KC_KEY_AUX, KC_EV_DOUBLE_CLICK, 2400, &t) == KC_NONE && kc_keys_brightness(&k, 2400));
    assert(kc_key_event(&k, KC_KEY_AUX, KC_EV_CLICK, 3000, &t) == KC_NONE && t == 0);          /* toggle back */
    assert(!kc_keys_brightness(&k, 3000));
    assert(kc_key_event(&k, KC_KEY_NEXT, KC_EV_CLICK, 3100, &t) == KC_NEXT_PAGE && t == 1);
    assert(kc_key_event(&k, KC_KEY_NEXT, KC_EV_LONG, 3200, &t) == KC_NONE && t == 0);          /* page mode: held does nothing */
}

static void test_brightness_mode_ends(void) {
    kc_keys_t k = {0}; uint8_t t;
    kc_key_event(&k, KC_KEY_AUX, KC_EV_CLICK, 5000, &t);
    kc_key_event(&k, KC_KEY_NEXT, KC_EV_CLICK, 6000, &t);                           /* re-arms the 15 s */
    assert(kc_keys_brightness(&k, 6000 + KC_BRIGHTNESS_IDLE_MS - 1));
    assert(kc_key_event(&k, KC_KEY_NEXT, KC_EV_CLICK, 6000 + KC_BRIGHTNESS_IDLE_MS, &t) == KC_NEXT_PAGE && t == 1);
    assert(!kc_keys_brightness(&k, 6000 + KC_BRIGHTNESS_IDLE_MS));

    k = (kc_keys_t){0};                                                             /* the clock wraps */
    kc_key_event(&k, KC_KEY_AUX, KC_EV_CLICK, 0xFFFFFF00u, &t);
    assert(kc_key_event(&k, KC_KEY_NEXT, KC_EV_CLICK, 0x00000100u, &t) == KC_BRIGHT_UP);
    assert(!kc_keys_brightness(&k, 0x00000100u + KC_BRIGHTNESS_IDLE_MS));

    k = (kc_keys_t){0};                                                             /* long press: sleep, page mode */
    kc_key_event(&k, KC_KEY_AUX, KC_EV_CLICK, 100, &t);
    assert(kc_key_event(&k, KC_KEY_AUX, KC_EV_LONG, 900, &t) == KC_SLEEP && t == 1);
    assert(!kc_keys_brightness(&k, 900));
    for (int i = 1; i <= KC_CLEAR_BOND_HOLDS; i++) {                                /* the forget count is intact */
        kc_action_t a = kc_key_event(&k, KC_KEY_AUX, KC_EV_HOLD, 900 + 150u * i, &t);
        assert((a == KC_CLEAR_BOND) == (i == KC_CLEAR_BOND_HOLDS));
    }

    k = (kc_keys_t){0};                                                             /* link change: page mode */
    kc_key_event(&k, KC_KEY_AUX, KC_EV_CLICK, 100, &t);
    kc_keys_page_mode(&k);
    assert(!kc_keys_brightness(&k, 100));
}
```

- [ ] **Step 2: Run them.** `make fw-test`. Expected: compile error (too many arguments to `kc_key_event`, `kc_keys_brightness` undeclared).
- [ ] **Step 3: Implement** the struct fields, the macro and the three functions as listed under Interfaces. `kc_keys.h` needs `#include <stdbool.h>`. Update the header comments that describe `kc_key_event`.
- [ ] **Step 4: Run them.** `make fw-test`. Expected: `ok test_brightness_mode`, `ok test_brightness_mode_ends` and every earlier `ok` line, exit 0.

### Task 3: App glue, LED, and the button table

**Files:**
- Modify: `fw/src/app_kindle_click.c` (`key_event`, `led`, `ble_status_event`)
- Modify: `fw/README.md` (add the button table)

**Interfaces:**
- Consumes: `kc_key_event(..., now_ms, ...)`, `kc_keys_brightness`, `kc_keys_page_mode` from Task 2. SDK: `u32 sys_timer_get_ms(void)` (10 ms steps), `u16 sys_timeout_add(void *priv, void (*func)(void *priv), u32 msec)`, the existing `led(u8 mode)` and `PWM_LED0_ON`.
- Produces: no new interface. Behavior:
  - `key_event` passes `sys_timer_get_ms()` to `kc_key_event`, then refreshes the LED.
  - The LED shows `PWM_LED0_ON` in brightness mode, and the link's pattern otherwise. Keep the link's pattern in one static (`PWM_LED0_SLOW_FLASH` while advertising, `PWM_LED0_ONE_FLASH_5S` while connected), set in `ble_status_event`.
  - While in brightness mode, a 1 s one-shot timer re-arms itself and refreshes the LED, so the LED follows the 15 s return without a key event. It is not running in page mode. (As built, after the review: one shot for the exact time left, from `kc_keys_brightness_left`, so the LED never lags the mode.)
  - `BLE_ST_CONNECT` and `BLE_ST_DISCONN` call `kc_keys_page_mode(&keys)` and refresh the LED.
  - `kc_send_press` needs no change: it queues any action, and `kc_report_press` builds the report.

- [ ] **Step 1: Make the changes** in `app_kindle_click.c`.
- [ ] **Step 2: Add the button table** to `fw/README.md`, in the "E2 board" section after the pin table: the table from the spec's "Behavior" section, and one line saying the LED is steady on in brightness mode.
- [ ] **Step 3: Run the host tests.** `make test`. Expected: every `ok` line, exit 0.
- [ ] **Step 4: Build.** `make fw-build`. Expected last lines: `app.bin: <size> bytes, starts ee ff ...`, `E2 update: key entry compiled in`, `E2 power: wake-up pins compiled in: key1 key2 usb-sense`. No compiler error.
- [ ] **Step 5: Pack.** `make fw-image base=firmware/e2control-zoom-phone-tablet.bin`. Expected: `fw/out/e2-kindle.bin` written, no error from the packer's self-check.

### Task 4: Flash and check on the unit

Needs the owner at the device and the owner's go-ahead for the flash.

**Files:**
- Modify: `docs/update-path.md` (record what was observed)

- [ ] **Step 1: Ask the owner for the go-ahead** to flash `fw/out/e2-kindle.bin`. Do not continue without it.
- [ ] **Step 2: Flash.** With the E2 on USB in normal mode (`ls /dev/cu.usbmodem*` shows one port):

```sh
python3 -I tools/e2boot.py enter /dev/cu.usbmodem<N> --yes
python3 -I tools/e2boot.py flash fw/out/e2-kindle.bin --yes
python3 -I tools/e2boot.py reboot --yes
```

Expected: `written and verified`, then the serial port is back within 10 s.
- [ ] **Step 3: Owner checks on the Kindle**, with a book open:
  1. Next and previous turn pages (page mode after start).
  2. A short press on the third button: the LED turns steady on. Next makes the screen brighter, previous dimmer. Holding either keeps changing it.
  3. A second short press: next and previous turn pages again.
  4. Short press, then wait 15 s without pressing: the LED leaves steady on within about a second, and next turns a page.
  5. Short press, then a long press on the third button: the Kindle sleeps. After waking it, next turns a page.
- [ ] **Step 4: Record the results** in `docs/update-path.md` under a dated heading: what was observed for each of the five checks, and whether the LED lit. If the LED did not light, say so; the mode must still work.
