/* Button events to actions. Pure C, no SDK includes. */
#ifndef KC_KEYS_H
#define KC_KEYS_H

#include <stdint.h>
#include "kc_proto.h"

#define KC_CLEAR_BOND_HOLDS 183   /* same count as stock */

/* key: SDK key number. event: SDK event (0 click, 1 long, 2 hold, 3 up, 4 double click).
 * hold_count: number of hold events so far, for event 2. */
kc_action_t kc_key_action(uint8_t key, uint8_t event, uint16_t hold_count);

#endif
