/* Button events to actions. Pure C, no SDK includes. */
#ifndef KC_KEYS_H
#define KC_KEYS_H

#include <stdbool.h>
#include <stdint.h>
#include "kc_proto.h"

#define KC_CLEAR_BOND_HOLDS 183   /* same count as stock */
#define KC_BRIGHTNESS_IDLE_MS 15000u    /* brightness mode ends this long after the last key event */
/* A key event arrives after its press: a click 0.2 s after the release (the SDK key driver waits for a second
 * press). An event that arrives up to this long after the mode ended was pressed while the mode was still shown. */
#define KC_KEY_LAG_MS 400u

/* SDK key numbers of this board (AD/IO key values). */
#define KC_KEY_PREV 1
#define KC_KEY_NEXT 2
#define KC_KEY_AUX  3

/* SDK key events (KEY_EVENT_*; app_kindle_click.c checks the values at compile time). */
#define KC_EV_CLICK         0
#define KC_EV_LONG          1
#define KC_EV_HOLD          2
#define KC_EV_UP            3
#define KC_EV_DOUBLE_CLICK  4
#define KC_EV_TRIPLE_CLICK  5
#define KC_EV_FOURTH_CLICK  6
#define KC_EV_FIFTH_CLICK   7

/* The page-mode mapping. key: SDK key number. event: SDK event (KC_EV_*). hold_count: number of hold events so far,
 * for KC_EV_HOLD. */
kc_action_t kc_key_action(uint8_t key, uint8_t event, uint16_t hold_count);

/* How many times the key's CLICK action applies for this event: 1 click, 2 double, 3 triple,
 * 4 for 4 or 5 clicks (KC_EV_FOURTH_CLICK, KC_EV_FIFTH_CLICK; capped at KC_QUEUE_LEN). 0 for any other event and for keys
 * whose click has no action. Take the action from kc_key_action(key, 0, 0), not from the
 * multi-click event, which kc_key_action maps to KC_NONE. */
uint8_t kc_key_repeat(uint8_t key, uint8_t event);

/* Key state kept between events. Zero-initialise it: page mode. */
typedef struct {
    uint16_t aux_holds;       /* consecutive KC_EV_HOLD events on KC_KEY_AUX */
    bool     brightness_mode; /* next and previous change the brightness; read it through kc_keys_brightness_ms_left */
    uint32_t last_event_ms;   /* time of the last key event in brightness mode */
} kc_keys_t;

/* The one call for the application: feed every SDK key event, get the action and how many times to apply it
 * (*times is 1 for a single action, 2..4 for a multi-click on a page key, 0 with KC_NONE). Counts the hold events
 * itself, so KC_CLEAR_BOND comes exactly once, on hold number KC_CLEAR_BOND_HOLDS.
 * A click on KC_KEY_AUX switches between page mode and brightness mode. In brightness mode the page keys give
 * KC_BRIGHT_DOWN (previous) and KC_BRIGHT_UP (next): per click as in page mode, and once per long and hold event.
 * Brightness mode ends KC_BRIGHTNESS_IDLE_MS after the last key event made in it.
 * now_ms: a millisecond clock that may wrap. */
kc_action_t kc_key_event(kc_keys_t *st, uint8_t key, uint8_t event, uint32_t now_ms, uint8_t *times);

/* Milliseconds until brightness mode ends by itself; 0 in page mode. For what shows the mode (the LED) and the
 * caller's timer for its end. kc_key_event still counts an event as made in brightness mode for KC_KEY_LAG_MS
 * after that end. */
uint32_t kc_keys_brightness_ms_left(const kc_keys_t *st, uint32_t now_ms);

/* Back to page mode: the link changed. */
void kc_keys_page_mode(kc_keys_t *st);

#endif
