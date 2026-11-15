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

uint32_t kc_keys_brightness_ms_left(const kc_keys_t *st, uint32_t now_ms)
{
    uint32_t idle = now_ms - st->last_event_ms;

    return st->brightness_mode && idle < KC_BRIGHTNESS_IDLE_MS ? KC_BRIGHTNESS_IDLE_MS - idle : 0;
}

void kc_keys_page_mode(kc_keys_t *st)
{
    st->brightness_mode = false;
}

kc_action_t kc_key_event(kc_keys_t *st, uint8_t key, uint8_t event, uint32_t now_ms, uint8_t *times)
{
    kc_action_t a;

    /* The mode ended KC_BRIGHTNESS_IDLE_MS after the last event, but this event's press is older than now_ms.
     * ponytail: the flag stays set until the next event, so a first event after exactly one clock wrap (49.7 days
     * with no key event) would count as made in brightness mode. Clear it from a timer if that ever matters. */
    if (now_ms - st->last_event_ms >= KC_BRIGHTNESS_IDLE_MS + KC_KEY_LAG_MS) {
        st->brightness_mode = false;
    }
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

    if (key == KC_KEY_AUX && event == KC_EV_CLICK) {
        st->brightness_mode = !st->brightness_mode;
    } else if (a == KC_SLEEP) {
        st->brightness_mode = false;
    } else if (st->brightness_mode && (key == KC_KEY_PREV || key == KC_KEY_NEXT)) {
        if (event == KC_EV_LONG || event == KC_EV_HOLD) {
            *times = 1;                 /* held: one step per event */
        }
        a = *times ? (key == KC_KEY_NEXT ? KC_BRIGHT_UP : KC_BRIGHT_DOWN) : KC_NONE;
    }
    if (st->brightness_mode) {
        st->last_event_ms = now_ms;
    }
    return a;
}
