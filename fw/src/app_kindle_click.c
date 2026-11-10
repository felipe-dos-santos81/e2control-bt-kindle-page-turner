/* Kindle Click compatible page turner: SDK glue around kc_proto / kc_keys / kc_link.
 * Started from apps/hid/examples/keypage/app_keypage.c (power, auto power-off, BT start-up),
 * BLE only. The HOGP profile, advertising data and security settings live in
 * apps/hid/modules/bt/ble_hogp.c, changed by fw/patches/0003-kindle-click-app.patch.
 *
 * Every function here runs in the app task: key events, BLE events (posted by ble_hogp.c
 * with sys_event_notify) and sys_timer/sys_timeout callbacks (run in the task that added them). */
#include "system/app_core.h"
#include "system/includes.h"
#include "app_config.h"
#include "app_action.h"
#include "btstack/btstack_task.h"
#include "btstack/avctp_user.h"
#include "bt_common.h"
#include "le_common.h"
#include "app_charge.h"
#include "app_power_manage.h"
#include "app_comm_bt.h"
#include "gatt_common/le_gatt_common.h"
#include "kc_proto.h"
#include "kc_keys.h"
#include "kc_link.h"

#if CONFIG_APP_KINDLE_CLICK

#define LOG_TAG_CONST       KEYPAGE     /* reuses the keypage switches in log_config.c */
#define LOG_TAG             "[KC]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#include "debug.h"

#define KC_TICK_MS          20
/* sys_timer_get_ms() counts in 10 ms steps, so 40 here is always more than 30 ms of real time. */
#define KC_KEY_DOWN_MS      40          /* key-down to key-up */
#define KC_KEY_GAP_MS       40          /* key-up to the next key-down */
#define KC_ADV_TIMEOUT_MS   (180 * 1000)

extern int  ble_hid_data_send(u8 report_id, u8 *data, u16 len);
extern int  ble_hid_is_connected(void);
extern void ble_module_enable(u8 en);
extern void le_hogp_set_PNP_info(const u8 *info);
extern void le_hogp_set_pair_allow(void);
extern void bt_pll_para(u32 osc, u32 sys, u8 low_power, u8 xosc);

static kc_link_t   kl;
static kc_action_t cur;             /* taken from the queue, key-down not sent yet */
static u8          key_down;        /* key-down sent, key-up still to send */
static u32         last_report_ms;
static u16         tick_timer;
static u16         adv_timer;
static u16         holds[KEY_NUM_MAX];  /* consecutive HOLD events per SDK key number */
static volatile u8 is_active;       /* 1: keep the system out of low power (soft power-off running) */
#if TCFG_HID_AUTO_SHUTDOWN_TIME
static u16         auto_shutdown_timer;
#endif

/* report_map_size is filled in at start: kc_report_map_len is not a constant expression. */
static ble_init_cfg_t kc_ble_config = {
    .same_address = 0,
    .appearance   = BLE_APPEARANCE_HID_KEYBOARD,
    .report_map   = kc_report_map,
};

/*------------------------------------------------------------------ report path */
static void tick(void *priv);

static void tick_start(void)
{
    if (!tick_timer) {
        tick_timer = sys_timer_add(NULL, tick, KC_TICK_MS);
    }
}

static void tick_stop(void)
{
    if (tick_timer) {
        sys_timer_del(tick_timer);
        tick_timer = 0;
    }
}

static int send_report(u8 *r)
{
    return ble_hid_data_send(KC_REPORT_ID, r, KC_REPORT_LEN);
}

/* Not ready; queued presses (and cur) are kept for the next link. */
static void link_down(void)
{
    kc_link_on_disconnect(&kl);
    key_down = 0;                   /* the host drops key state with the link */
}

/* Runs only while there is work: started by a press or a link event, stops itself when the
 * queue is drained on a ready link or when there is no link. One report per tick, so a press
 * is key-down, key-up >= KC_KEY_DOWN_MS later, next key-down >= KC_KEY_GAP_MS after that. */
static void tick(void *priv)
{
    u8 r[KC_REPORT_LEN];
    u32 now = sys_timer_get_ms();

    if (!ble_hid_is_connected()) {
        link_down();
        tick_stop();
        return;
    }
    if ((u32)(now - last_report_ms) < (key_down ? KC_KEY_DOWN_MS : KC_KEY_GAP_MS)) {
        return;
    }
    if (key_down) {
        kc_report_release(r);
        if (send_report(r) == 0) {  /* on failure retry next tick: a lost key-up would leave the key held */
            key_down = 0;
            last_report_ms = now;
        }
        return;
    }
    if (cur == KC_NONE) {
        cur = kc_link_pop(&kl, now);
        if (cur == KC_NONE) {
            if (kc_link_ready(&kl, now)) {
                tick_stop();        /* ready and nothing queued */
            }
            return;
        }
    }
    if (!kc_link_ready(&kl, now)) {
        return;                     /* cur was taken on an earlier link */
    }
    kc_report_press(cur, r);
    if (send_report(r) == 0) {
        cur = KC_NONE;
        key_down = 1;
        last_report_ms = now;
    }
}

/*------------------------------------------------------------------ advertising, bond */
static void adv_timeout(void *priv)
{
    adv_timer = 0;
    if (!ble_hid_is_connected() && ble_gatt_server_get_work_state() == BLE_ST_ADV) {
        log_info("adv timeout: stop, sleep until a key");
        ble_gatt_server_adv_enable(0);  /* nothing else keeps the chip awake: it powers down, a key wakes it */
    }
}

static void adv_timer_stop(void)
{
    if (adv_timer) {
        sys_timeout_del(adv_timer);
        adv_timer = 0;
    }
}

static void clear_bond(void)
{
    log_info("clear bond");
    ble_module_enable(0);           /* stop advertising, drop the link */
    le_hogp_set_pair_allow();       /* forget the peer: hogp pair info and the stack's bond list in VM */
    kc_link_reset(&kl);
    cur = KC_NONE;
    key_down = 0;
    tick_stop();
    ble_module_enable(1);           /* advertise again, at once or when the disconnect completes */
}

/*------------------------------------------------------------------ keys */
static void key_event(u8 key, u8 event)
{
    u16 hold_count = 0;
    u8 n = 1;
    kc_action_t a;

    if (key < KEY_NUM_MAX) {
        if (event == KEY_EVENT_HOLD) {
            if (holds[key] < 0xffff) {
                holds[key]++;
            }
            hold_count = holds[key];
        } else {
            holds[key] = 0;
        }
    }
    /* The key driver reports quick repeats as one multi-click event; each click is a page. */
    if (event >= KEY_EVENT_DOUBLE_CLICK && event <= KEY_EVENT_FIRTH_CLICK) {
        n = event - KEY_EVENT_DOUBLE_CLICK + 2;
        event = KEY_EVENT_CLICK;
    }

    if (!ble_hid_is_connected() && ble_gatt_server_get_work_state() != BLE_ST_ADV) {
        log_info("key: advertise again");
        ble_gatt_server_adv_enable(1);
    }

    a = kc_key_action(key, event, hold_count);
    if (a == KC_CLEAR_BOND) {
        clear_bond();
        return;
    }
    if (a == KC_NONE) {
        return;
    }
    while (n--) {
        kc_link_push(&kl, a);     /* full queue: this press is dropped */
    }
    tick_start();
}

/*------------------------------------------------------------------ BLE events */
static void ble_status_event(u8 status)
{
    switch (status) {
    case BLE_ST_ADV:
        adv_timer_stop();
        adv_timer = sys_timeout_add(NULL, adv_timeout, KC_ADV_TIMEOUT_MS);
        break;
    case BLE_ST_CONNECT:
        adv_timer_stop();
        link_down();                /* new link: not ready until encrypted */
        break;
    case BLE_ST_DISCONN:
        link_down();
        break;
    case BLE_PRIV_PAIR_ENCRYPTION_CHANGE:
        kc_link_on_encrypted(&kl, sys_timer_get_ms());
        tick_start();
        break;
    default:
        break;
    }
}

static void common_event(struct bt_event *bt)
{
    switch (bt->event) {
    case COMMON_EVENT_BLE_CCCD_WRITE:
        kc_link_on_cccd_write(&kl, sys_timer_get_ms());
        tick_start();
        break;
#if TCFG_HID_AUTO_SHUTDOWN_TIME
    case COMMON_EVENT_SHUTDOWN_DISABLE:
        if (auto_shutdown_timer) {
            sys_timeout_del(auto_shutdown_timer);
        }
        break;
#endif
    default:                        /* COMMON_EVENT_BLE_REMOTE_TYPE: one report map for every host */
        break;
    }
}

/*------------------------------------------------------------------ power, as app_keypage.c */
static void power_event_to_user(void *priv)
{
    struct sys_event e;
    e.type = SYS_DEVICE_EVENT;
    e.arg  = (void *)DEVICE_EVENT_FROM_POWER;
    e.u.dev.event = (u32)priv;
    e.u.dev.value = 0;
    sys_event_notify(&e);
}

static void set_soft_poweroff(void)
{
    log_info("soft poweroff");
    is_active = 1;
    btstack_ble_exit(0);
    sys_timeout_add(NULL, power_set_soft_poweroff, WAIT_DISCONN_TIME_MS);   /* let the link close first */
}

static void app_start(void)
{
    log_info("kindle click, BLE only");
    clk_set("sys", BT_NORMAL_HZ);
    bt_pll_para(TCFG_CLOCK_OSC_HZ, clk_get("sys"), 0, 0);

    kc_link_reset(&kl);
    kc_ble_config.report_map_size = kc_report_map_len;
    btstack_ble_start_before_init(&kc_ble_config, 0);
    le_hogp_set_PNP_info(kc_pnp_id);
    btstack_init();

    sys_key_event_enable();
#if TCFG_HID_AUTO_SHUTDOWN_TIME
    auto_shutdown_timer = sys_timeout_add((void *)POWER_EVENT_POWER_SOFTOFF, power_event_to_user, TCFG_HID_AUTO_SHUTDOWN_TIME * 1000);
#endif
}

static int state_machine(struct application *app, enum app_state state, struct intent *it)
{
    if (state == APP_STA_START && it && it->action == ACTION_KINDLE_CLICK) {
        app_start();
    }
    return 0;
}

static int event_handler(struct application *app, struct sys_event *event)
{
#if TCFG_HID_AUTO_SHUTDOWN_TIME
    if (event->type != SYS_DEVICE_EVENT || DEVICE_EVENT_FROM_POWER != event->arg) {
        sys_timer_modify(auto_shutdown_timer, TCFG_HID_AUTO_SHUTDOWN_TIME * 1000);
    }
#endif

    switch (event->type) {
    case SYS_KEY_EVENT:
        if (event->arg == (void *)DEVICE_EVENT_FROM_KEY) {
            key_event(event->u.key.value, event->u.key.event);
        }
        return 0;

    case SYS_BT_EVENT:
        if ((u32)event->arg == SYS_BT_EVENT_TYPE_CON_STATUS) {
            if (event->u.bt.event == BT_STATUS_INIT_OK) {
                btstack_ble_start_after_init(0);
            }
        } else if ((u32)event->arg == SYS_BT_EVENT_TYPE_HCI_STATUS) {
            bt_comm_ble_hci_event_handler(&event->u.bt);
        } else if ((u32)event->arg == SYS_BT_EVENT_BLE_STATUS) {
            ble_status_event(event->u.bt.event);
        } else if ((u32)event->arg == SYS_BT_EVENT_FORM_COMMON) {
            common_event(&event->u.bt);
        }
        return 0;

    case SYS_DEVICE_EVENT:
        if ((u32)event->arg == DEVICE_EVENT_FROM_POWER) {
            return app_power_event_handler(&event->u.dev, set_soft_poweroff);
        }
#if TCFG_CHARGE_ENABLE
        if ((u32)event->arg == DEVICE_EVENT_FROM_CHARGE) {
            app_charge_event_handler(&event->u.dev);
        }
#endif
        return 0;

    default:
        return 0;
    }
}

static u8 idle_query(void)
{
    return !is_active;
}

REGISTER_LP_TARGET(app_kindle_click_lp_target) = {
    .name = "app_kindle_click",
    .is_idle = idle_query,
};

static const struct application_operation app_kindle_click_ops = {
    .state_machine = state_machine,
    .event_handler = event_handler,
};

REGISTER_APPLICATION(app_kindle_click) = {
    .name   = "kindle_click",
    .action = ACTION_KINDLE_CLICK,
    .ops    = &app_kindle_click_ops,
    .state  = APP_STA_DESTROY,
};

#endif
