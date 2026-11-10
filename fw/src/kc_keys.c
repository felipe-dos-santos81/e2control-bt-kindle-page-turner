#include "kc_keys.h"

enum { EV_CLICK = 0, EV_LONG = 1, EV_HOLD = 2 };

kc_action_t kc_key_action(uint8_t key, uint8_t event, uint16_t hold_count)
{
    if (event == EV_CLICK) {
        if (key == 1) return KC_PREV_PAGE;
        if (key == 2) return KC_NEXT_PAGE;
        return KC_NONE;
    }
    if (key == 3) {
        if (event == EV_LONG) return KC_SLEEP;
        if (event == EV_HOLD && hold_count == KC_CLEAR_BOND_HOLDS) return KC_CLEAR_BOND;
    }
    return KC_NONE;
}
