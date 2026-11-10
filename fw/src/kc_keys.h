/* Button events to actions. Pure C, no SDK includes. */
#ifndef KC_KEYS_H
#define KC_KEYS_H

#include <stdint.h>
#include "kc_proto.h"

#define KC_CLEAR_BOND_HOLDS 183   /* same count as stock */

/* key: SDK key number. event: SDK event (0 click, 1 long, 2 hold, 3 up, 4 double click).
 * hold_count: number of hold events so far, for event 2. */
kc_action_t kc_key_action(uint8_t key, uint8_t event, uint16_t hold_count);

/* How many times the key's CLICK action applies for this event: 1 click, 2 double, 3 triple,
 * 4 for 4 or 5 clicks (events 6, 7; capped at KC_QUEUE_LEN). 0 for any other event and for keys
 * whose click has no action. Take the action from kc_key_action(key, 0, 0), not from the
 * multi-click event, which kc_key_action maps to KC_NONE. */
uint8_t kc_key_repeat(uint8_t key, uint8_t event);

#endif
