#include "kc_keys.h"
#include "kc_link.h"

kc_action_t kc_key_action(uint8_t key, uint8_t event, uint16_t hold_count)
{
    if (event == KC_EV_CLICK) {
        if (key == KC_KEY_PREV) return KC_PREV_PAGE;
        if (key == KC_KEY_NEXT) return KC_NEXT_PAGE;
        return KC_NONE;
    }
    if (key == KC_KEY_AUX) {
        if (event == KC_EV_LONG) return KC_SLEEP;
        if (event == KC_EV_HOLD && hold_count == KC_CLEAR_BOND_HOLDS) return KC_CLEAR_BOND;
    }
    return KC_NONE;
}

uint8_t kc_key_repeat(uint8_t key, uint8_t event)
{
    uint8_t n;

    if (kc_key_action(key, KC_EV_CLICK, 0) == KC_NONE) return 0;
    if (event == KC_EV_CLICK) return 1;
    if (event < KC_EV_DOUBLE_CLICK || event > KC_EV_FIFTH_CLICK) return 0;
    n = (uint8_t)(event - KC_EV_DOUBLE_CLICK + 2);
    return n > KC_QUEUE_LEN ? KC_QUEUE_LEN : n;
}

kc_action_t kc_key_event(kc_keys_t *st, uint8_t key, uint8_t event, uint8_t *times)
{
    kc_action_t a;

    if (key == KC_KEY_AUX) {
        if (event != KC_EV_HOLD) {
            st->aux_holds = 0;
        } else if (st->aux_holds < UINT16_MAX) {
            st->aux_holds++;
        }
    }
    *times = kc_key_repeat(key, event);
    a = *times ? kc_key_action(key, KC_EV_CLICK, 0)
               : kc_key_action(key, event, key == KC_KEY_AUX ? st->aux_holds : 0);
    if (a != KC_NONE && *times == 0) {
        *times = 1;
    }
    return a;
}
