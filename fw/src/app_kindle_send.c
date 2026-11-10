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

static kc_link_t   kl;
static u8          pending;         /* presses in kl (kc_link_t keeps its count private) */
static kc_action_t cur;             /* taken from kl, key-down not sent yet */
static u8          key_down;        /* key-down sent, key-up still to send */
static u8          encrypted;       /* this link is encrypted */
static u32         last_report_ms;
static u16         tick_timer;

static void tick(void *priv);

static bool have_work(void)
{
    return pending || cur != KC_NONE || key_down;
}

static void tick_stop(void)
{
    if (tick_timer) {
        sys_timer_del(tick_timer);
        tick_timer = 0;
    }
}

/* The tick runs only while there is something to send on an encrypted link. */
static void kick(void)
{
    if (!tick_timer && have_work() && encrypted) {
        tick_timer = sys_timer_add(NULL, tick, KC_TICK_MS);
    }
}

/* One report per tick: key-down, key-up >= KC_KEY_DOWN_MS later, next key-down
 * >= KC_KEY_GAP_MS after that. */
static void tick(void *priv)
{
    u8 r[KC_REPORT_LEN];
    u32 now = sys_timer_get_ms();
    int err;

    if (!ble_hid_is_connected()) {
        kc_send_link_down();
    }
    if (!encrypted || !have_work()) {
        tick_stop();                /* restarted by kc_send_encrypted or a new press */
        return;
    }
    if ((u32)(now - last_report_ms) < (key_down ? KC_KEY_DOWN_MS : KC_KEY_GAP_MS)) {
        return;
    }
    if (!key_down && cur == KC_NONE) {
        cur = kc_link_pop(&kl, now);
        if (cur == KC_NONE) {
            return;                 /* not ready yet: at most KC_READY_AFTER_ENC_MS after encryption */
        }
        pending--;
    }
    if (!key_down && !kc_link_ready(&kl, now)) {
        return;                     /* cur was taken on an earlier link */
    }
    if (key_down) {
        kc_report_release(r);
    } else {
        kc_report_press(cur, r);
    }
    err = ble_hid_data_send(KC_REPORT_ID, r, KC_REPORT_LEN);
    if (err == GATT_CMD_USE_CCC_FAIL) {
        /* The host has not enabled the input report: nothing was sent (user_data_att_send in
         * btstack.a checks the CCCD). Keep cur; the next CCCD write restarts the tick. */
        key_down = 0;
        tick_stop();
        return;
    }
    if (err) {
        return;                     /* buffer full etc.: retry next tick, so a key-up is never lost */
    }
    last_report_ms = now;
    if (key_down) {
        key_down = 0;
    } else {
        cur = KC_NONE;
        key_down = 1;
    }
}

void kc_send_reset(void)
{
    kc_link_reset(&kl);
    pending = 0;
    cur = KC_NONE;
    key_down = 0;
    encrypted = 0;
    tick_stop();
}

void kc_send_press(kc_action_t a, u8 times)
{
    while (times--) {
        pending += kc_link_push(&kl, a);
    }
    kick();
}

void kc_send_link_down(void)
{
    kc_link_on_disconnect(&kl);
    key_down = 0;                   /* the host drops key state with the link */
    encrypted = 0;
}

void kc_send_encrypted(void)
{
    kc_link_on_encrypted(&kl, sys_timer_get_ms());
    encrypted = 1;
    kick();
}

void kc_send_cccd_written(void)
{
    kc_link_on_cccd_write(&kl, sys_timer_get_ms());
    kick();
}

#endif
