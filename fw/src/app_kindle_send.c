/* Report path of the Kindle Click app (see app_kindle_send.h). */
#include "system/includes.h"
#include "app_config.h"
#include "gatt_common/le_gatt_common.h"
#include "kc_link.h"
#include "app_kindle_send.h"

#if CONFIG_APP_KINDLE_CLICK

#define KC_TICK_MS          20
/* sys_timer_get_ms() counts in 10 ms steps, so 40 here is always more than 30 ms of real time. */
#define KC_KEY_DOWN_MS      40          /* key-down to key-up */
#define KC_KEY_GAP_MS       40          /* key-up to the next key-down */

extern int ble_hid_data_send(u8 report_id, u8 *data, u16 len);
extern int ble_hid_is_connected(void);

static kc_link_t   link;
static kc_action_t current_action;  /* taken from link, key-down not sent yet */
static u8          key_down;        /* key-down sent, key-up still to send */
static u32         last_report_ms;
static u16         send_tick_timer;

static void send_tick(void *priv);

static bool have_work(void)
{
    return kc_link_count(&link) || current_action != KC_NONE || key_down;
}

static void send_tick_stop(void)
{
    if (send_tick_timer) {
        sys_timer_del(send_tick_timer);
        send_tick_timer = 0;
    }
}

/* The tick runs only while there is something to send on an encrypted link. */
static void kick(void)
{
    if (!send_tick_timer && have_work() && kc_link_is_encrypted(&link)) {
        send_tick_timer = sys_timer_add(NULL, send_tick, KC_TICK_MS);
    }
}

/* One report per tick: key-down, key-up >= KC_KEY_DOWN_MS later, next key-down
 * >= KC_KEY_GAP_MS after that. */
static void send_tick(void *priv)
{
    u8 r[KC_REPORT_LEN];
    u32 now = sys_timer_get_ms();
    int err;

    if (!ble_hid_is_connected()) {
        kc_send_link_down();
    }
    if (!kc_link_is_encrypted(&link) || !have_work()) {
        send_tick_stop();           /* restarted by kc_send_encrypted or a new press */
        return;
    }
    if ((u32)(now - last_report_ms) < (key_down ? KC_KEY_DOWN_MS : KC_KEY_GAP_MS)) {
        return;
    }
    if (!key_down && current_action == KC_NONE) {
        current_action = kc_link_pop(&link, now);
        if (current_action == KC_NONE) {
            return;                 /* not ready yet: at most KC_READY_AFTER_ENC_MS after encryption */
        }
    }
    if (!key_down && !kc_link_ready(&link, now)) {
        return;                     /* current_action was taken on an earlier link */
    }
    if (key_down) {
        kc_report_release(r);
    } else {
        kc_report_press(current_action, r);
    }
    err = ble_hid_data_send(KC_REPORT_ID, r, KC_REPORT_LEN);
    if (err == GATT_CMD_USE_CCC_FAIL) {
        /* The host has not enabled the input report: nothing was sent (user_data_att_send in
         * btstack.a checks the CCCD). Keep current_action; the next CCCD write restarts the tick. */
        key_down = 0;
        send_tick_stop();
        return;
    }
    if (err) {
        return;                     /* buffer full etc.: retry next tick, so a key-up is never lost */
    }
    last_report_ms = now;
    if (key_down) {
        key_down = 0;
    } else {
        current_action = KC_NONE;
        key_down = 1;
    }
}

void kc_send_reset(void)
{
    kc_link_reset(&link);
    current_action = KC_NONE;
    key_down = 0;
    send_tick_stop();
}

void kc_send_press(kc_action_t a, u8 times)
{
    while (times--) {
        kc_link_push(&link, a);
    }
    kick();
}

void kc_send_link_down(void)
{
    kc_link_on_disconnect(&link);
    key_down = 0;                   /* the host drops key state with the link */
}

void kc_send_encrypted(void)
{
    kc_link_on_encrypted(&link, sys_timer_get_ms());
    kick();
}

void kc_send_cccd_written(void)
{
    kc_link_on_cccd_write(&link, sys_timer_get_ms());
    kick();
}

#endif
