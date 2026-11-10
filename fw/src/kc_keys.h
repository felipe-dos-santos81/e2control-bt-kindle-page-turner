/* Button events to actions. Pure C, no SDK includes. */
#ifndef KC_KEYS_H
#define KC_KEYS_H

#include <stdint.h>
#include "kc_proto.h"

#define KC_CLEAR_BOND_HOLDS 183   /* same count as stock */

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

/* key: SDK key number. event: SDK event (KC_EV_*). hold_count: number of hold events so far, for KC_EV_HOLD. */
kc_action_t kc_key_action(uint8_t key, uint8_t event, uint16_t hold_count);

/* How many times the key's CLICK action applies for this event: 1 click, 2 double, 3 triple,
 * 4 for 4 or 5 clicks (KC_EV_FOURTH_CLICK, KC_EV_FIFTH_CLICK; capped at KC_QUEUE_LEN). 0 for any other event and for keys
 * whose click has no action. Take the action from kc_key_action(key, 0, 0), not from the
 * multi-click event, which kc_key_action maps to KC_NONE. */
uint8_t kc_key_repeat(uint8_t key, uint8_t event);

#endif
