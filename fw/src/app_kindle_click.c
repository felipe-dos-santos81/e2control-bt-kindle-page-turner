/* Kindle Click compatible page turner: keys, BLE events, advertising, power. The report path is
 * app_kindle_send.c; the SDK-free units are kc_proto / kc_keys / kc_link.
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
#include "app_kindle_send.h"
#include "e2_update.h"
#include "e2_power.h"

#if CONFIG_APP_KINDLE_CLICK

#define LOG_TAG_CONST       KEYPAGE     /* reuses the keypage switches in log_config.c */
#define LOG_TAG             "[KC]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#include "debug.h"

/* kc_keys.h repeats the SDK key event values (it has no SDK includes): keep them equal. */
_Static_assert(KC_EV_CLICK == KEY_EVENT_CLICK, "KC_EV_CLICK");
_Static_assert(KC_EV_LONG == KEY_EVENT_LONG, "KC_EV_LONG");
_Static_assert(KC_EV_HOLD == KEY_EVENT_HOLD, "KC_EV_HOLD");
_Static_assert(KC_EV_UP == KEY_EVENT_UP, "KC_EV_UP");
_Static_assert(KC_EV_DOUBLE_CLICK == KEY_EVENT_DOUBLE_CLICK, "KC_EV_DOUBLE_CLICK");
_Static_assert(KC_EV_TRIPLE_CLICK == KEY_EVENT_TRIPLE_CLICK, "KC_EV_TRIPLE_CLICK");
_Static_assert(KC_EV_FOURTH_CLICK == KEY_EVENT_FOURTH_CLICK, "KC_EV_FOURTH_CLICK");
_Static_assert(KC_EV_FIFTH_CLICK == KEY_EVENT_FIRTH_CLICK, "KC_EV_FIFTH_CLICK");   /* sic: the SDK spells it FIRTH */

extern void ble_module_enable(u8 en);
extern void le_hogp_set_PNP_info(const u8 *info);
extern void le_hogp_set_pair_allow(void);
extern void bt_pll_para(u32 osc, u32 sys, u8 low_power, u8 xosc);

static u16         holds[KEY_NUM_MAX];  /* consecutive HOLD events per SDK key number */
static volatile u8 is_active;       /* 1: keep the system out of low power (soft power-off running) */

/* report_map_size is filled in at start: kc_report_map_len is not a constant expression. */
static ble_init_cfg_t kc_ble_config = {
    .same_address = 0,
    .appearance   = BLE_APPEARANCE_HID_KEYBOARD,
    .report_map   = kc_report_map,
};

/*------------------------------------------------------------------ bond */
static void clear_bond(void)
{
    log_info("clear bond");
    ble_module_enable(0);           /* stop advertising, drop the link */
    le_hogp_set_pair_allow();       /* forget the peer: hogp pair info and the stack's bond list in VM */
    kc_send_reset();
    ble_module_enable(1);           /* advertise again, at once or when the disconnect completes */
}

/*------------------------------------------------------------------ keys */
static void key_event(u8 key, u8 event)
{
    u16 hold_count = 0;
    u8 repeat = kc_key_repeat(key, event);  /* a multi-click is that many clicks */
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

    e2_power_on_key();              /* any key event re-arms the 30 min idle power-off */

    /* kc_key_action maps a multi-click event to KC_NONE: take the action from the CLICK event. */
    a = repeat ? kc_key_action(key, KEY_EVENT_CLICK, 0) : kc_key_action(key, event, hold_count);
    if (a == KC_CLEAR_BOND) {
        clear_bond();
    } else if (a != KC_NONE) {
        kc_send_press(a, repeat ? repeat : 1);
    }
}

/*------------------------------------------------------------------ BLE events */
static void ble_status_event(u8 status)
{
    switch (status) {
    case BLE_ST_ADV:
        e2_power_on_adv_start();    /* 180 s without a connection: power off (e2_power.c) */
        break;
    case BLE_ST_CONNECT:
        e2_power_on_connected();
        kc_send_link_down();        /* new link: not ready until encrypted */
        break;
    case BLE_ST_DISCONN:
        e2_power_on_disconnected();
        kc_send_link_down();
        break;
    case BLE_PRIV_PAIR_ENCRYPTION_CHANGE:
        kc_send_encrypted();
        break;
    default:
        break;
    }
}

static void common_event(struct bt_event *bt)
{
    switch (bt->event) {
    case COMMON_EVENT_BLE_CCCD_WRITE:
        kc_send_cccd_written();
        break;
    default:                        /* COMMON_EVENT_BLE_REMOTE_TYPE: one report map for every host */
        break;
    }
}

/*------------------------------------------------------------------ power */
/* The delayed half of the power-off, WAIT_DISCONN_TIME_MS after the request. USB power may have been plugged in since
 * the decision; that gives no wake-up edge once the unit is off, so it would stay off with the cable in. Restart
 * instead, so it boots with USB and BLE running again. */
static void power_off_or_restart(void *priv)
{
    if (e2_power_usb_present()) {
        log_info("USB power appeared, restart instead of power off");
        cpu_reset();                /* the SDK's system reset: p33_soft_reset, as app_comm_ble.c, uart_update.c, ... */
        return;
    }
    power_set_soft_poweroff();
}

/* The only way into a power-off, idempotent: the first request wins, the 1 s power poll is stopped whichever
 * path asked. Callers: e2_power.c's poll (reads the USB sense pin first) and low_battery_poweroff below. */
static void set_soft_poweroff(void)
{
    if (is_active) {
        return;
    }
    log_info("soft poweroff");
    is_active = 1;
    e2_power_stop();
    btstack_ble_exit(0);
    sys_timeout_add(NULL, power_off_or_restart, WAIT_DISCONN_TIME_MS);   /* let the link close first */
}

/* The SDK's low-battery event (app_power_event_handler, POWER_EVENT_POWER_LOW) ends here. With USB power present the
 * unit stays on. The SDK stopped its battery monitor before calling us: start it again, so a later low reading
 * (after unplugging) powers off. */
static void low_battery_poweroff(void)
{
    if (e2_power_usb_present()) {
        log_info("low battery, USB power present: staying on");
        vbat_check_init();
        return;
    }
    set_soft_poweroff();
}

static void app_start(void)
{
    e2_update_boot_keys();          /* keys 1 + 2 held at power-on: update mode, before BLE starts */
    log_info("kindle click, BLE only");
    clk_set("sys", BT_NORMAL_HZ);
    bt_pll_para(TCFG_CLOCK_OSC_HZ, clk_get("sys"), 0, 0);

    kc_send_reset();
    kc_ble_config.report_map_size = kc_report_map_len;
    btstack_ble_start_before_init(&kc_ble_config, 0);
    le_hogp_set_PNP_info(kc_pnp_id);
    btstack_init();

    sys_key_event_enable();
    e2_update_usb_start();          /* USB CDC for the update command, at the same point as stock */
    e2_power_init(set_soft_poweroff);
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
            return app_power_event_handler(&event->u.dev, low_battery_poweroff);
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
