# Brightness mode on the third button: design

Date: 2026-11-15. Status: approved by the owner on 2026-11-15.

This adds to `2026-11-09-kindle-click-firmware-design.md`. That design gave the third button one job, a long press that puts the Kindle to sleep. This design gives its short press a job too.

## Why

On 2026-11-15 the E2 paired with the Kindle Colorsoft as a "Kindle Click". Next and previous turn pages, and a long press on the third button puts the Kindle to sleep. A short press on the third button does nothing. The owner wants to change the screen brightness from the remote and asked for the third button to switch that on and off.

## Behavior

The remote has two modes. It starts in page mode.

| Input | Page mode | Brightness mode |
|---|---|---|
| Third button, short press | go to brightness mode | go to page mode |
| Next, short press | next page | one step brighter |
| Previous, short press | previous page | one step dimmer |
| Next or previous, double to fifth press | 2 to 4 page turns (as today) | 2 to 4 brightness steps |
| Next or previous, held | nothing (as today) | one step after 0.75 s, then one step every 0.15 s |
| Third button, long press | Kindle sleeps (as today) | Kindle sleeps; back to page mode |
| Third button, held about 28 s | forget the pairing (as today) | same |

The remote goes back to page mode by itself:

- 15 s after the last button event in brightness mode,
- when the link to the Kindle drops,
- after a long press on the third button.

The LED shows the mode: steady on in brightness mode, its usual pattern in page mode.

Owner decisions: the third button toggles the mode; in brightness mode next and previous change the brightness.

My calls, for the owner to correct: next is brighter and previous is dimmer; holding repeats; the 15 s return; the LED pattern; a long press still means sleep.

## Goals

- A short press on the third button switches the mode, every time.
- In brightness mode the Kindle's screen light follows next and previous.
- Page turning in page mode stays exactly as it is.

## Non-goals

- The side selector. It stays unused.
- Power-off, wake-up, battery level, reconnection timing. They are separate work.
- Colour temperature ("warmth"). The Kindle Click has no key for it.

## Anti-goals

- A page button that changes the brightness when the owner meant to turn a page. The 15 s return and the LED exist to prevent this.
- Any change to the identity bytes, the report map or the pairing settings. They work on the unit.

## Constraints

- The Kindle acts on two consumer usages for the light: `0x006F` (brightness up) and `0x0070` (brightness down). They go in bytes 8 and 9 of input report 1, low byte first, like the sleep usage `0x0030`. Source: `KindleClick.h` in Open-Kindle-Click-ESP32-C6, which works on Kindles. The report map already allows them.
- The SDK key driver reports events, not raw presses: a click arrives 0.2 s after the release, a long press after 0.75 s, then a hold event every 0.15 s (`apps/common/device/key/iokey.c`). There is no "button went down" event.
- Key logic lives in `fw/src/kc_keys.c`, pure C, host-tested. It has no clock. Report bytes live in `fw/src/kc_proto.c`.
- The press queue holds 4 presses (`kc_link`). A press is sent as key down, then key up at least 30 ms later.
- The LED pin (PB5) comes from the stock firmware and is not confirmed on the unit.
- Every flash needs the owner's go-ahead.

## Approaches

1. **Mode in the key logic, one tap per event (chosen).** `kc_keys` keeps the mode. Each click, long or hold event on a page button in brightness mode becomes one tap of the brightness key. It reuses the existing queue and send path, and all of it is host-testable. Cost: holding gives 6 to 7 steps a second, set by the SDK's hold period, not by the Kindle's own key repeat.
2. **Key held down for as long as the button is down**, as the reference remote does, so the Kindle repeats the key itself. The SDK key driver gives no press-down event, so this needs our own pin polling beside the key driver. More code on the pins for a small gain in feel.
3. **The side selector chooses the mode.** The owner asked for the third button, and the selector's wiring is not measured.

## How

- `kc_proto`: two new actions, brightness up and brightness down, with their report bytes.
- `kc_keys`: the mode in `kc_keys_t`; the mapping in the table above; the time of the last event, so that the 15 s return is decided here and tested on the host. The application passes the current time in.
- `app_kindle_click.c`: passes the time, sets the LED when the mode changes, and resets the mode on disconnect. The LED must also change when the 15 s run out with no button event.
- `fw/tests/test_kc.c`: tests for the toggle, both mappings, multi-press, hold repeat, the 15 s return, the reset on disconnect and on sleep, and the new report bytes.
- `fw/README.md`: the button table.

## Left to the builder

- How the 15 s return reaches the LED (a one-shot timer re-armed on each brightness-mode event is enough).
- Whether a double press on the third button counts as one toggle or two.
- Names.

## Done means

- Host tests pass.
- On the unit, after a flash the owner approves: a short press on the third button turns the LED on; next and previous then change the Kindle's brightness, also while held; a second short press, or 15 s, returns to page turning; long-press sleep still works.
- If the LED does not light, the mode still works; the LED pin is then a separate fix.

## As built (2026-11-15, after the reviews)

- The mode also returns to page mode when a link starts, not only when it drops. Without a link the remote stays in page mode.
- The third button is a click-only key in the SDK key driver. Its short press is reported at the release, not 0.2 s later, so a quick press on another button cannot cancel it. Each short press is one toggle; there are no double or triple presses on it.
- The 15 s return is judged at the press, not at the event: an event that arrives up to 0.4 s after the LED went off counts as made in brightness mode.
- If the firmware cannot start the timer for the 15 s return, it stays in page mode.
