/* Power policy: soft power-off after 180 s advertising without a connection, or 30 min connected without a
 * key event; never while USB power is present. Off while the board config has TCFG_E2_SELF_POWER_OFF 0.
 * Pins: board config (TCFG_E2_USB_SENSE_PORT, from the stock firmware, not measured on the board).
 * ponytail: one 1 s poll instead of one timer per rule; powers off up to 1 s late, and the poll keeps the
 * app task busy once a second (the chip does not sleep anyway: USB device up). */
#include "system/includes.h"
#include "app_config.h"
#include "asm/gpio.h"
#include "app_power_manage.h"
#include "kc_power.h"
#include "e2_power.h"

#define E2_POWER_POLL_MS    1000

static void (*power_off_cb)(void);
static u16  poll_timer;
static u8   connected;
static u32  unconnected_since_ms;   /* sys_timer_get_ms() at advertising start or at disconnect */
static u32  last_key_ms;        /* ... at the last key event (or at connect) */

u8 e2_power_usb_present(void)
{
#if E2_USB_SENSE_ENABLE
    return gpio_read(TCFG_E2_USB_SENSE_PORT) != 0;
#else
    return 0;
#endif
}

static void power_poll(void *priv)
{
    u32 now = sys_timer_get_ms();
    u8 usb = e2_power_usb_present();   /* read right before the decision, every time */

    if (usb) {                      /* a timer that would expire re-arms: counting restarts at unplug */
        unconnected_since_ms = now;
        last_key_ms  = now;
    }
    if (kc_power_should_off(usb, connected, now - unconnected_since_ms, now - last_key_ms)) {
        power_off_cb();             /* stops this poll through e2_power_stop */
    }
}

void e2_power_stop(void)
{
    if (poll_timer) {
        sys_timer_del(poll_timer);
        poll_timer = 0;
    }
}

void e2_power_init(void (*power_off)(void))
{
    power_off_cb = power_off;
    connected    = 0;
    unconnected_since_ms = last_key_ms = sys_timer_get_ms();
#if E2_USB_SENSE_ENABLE
    e2_input_pin(TCFG_E2_USB_SENSE_PORT, 1, 0);         /* no internal pull, as stock: the board drives the pin */
#endif
    if (TCFG_E2_SELF_POWER_OFF) {
        poll_timer = sys_timer_add(NULL, power_poll, E2_POWER_POLL_MS);
    }
}

void e2_power_on_adv_start(void)
{
    unconnected_since_ms = sys_timer_get_ms();
}

void e2_power_on_connected(void)
{
    connected   = 1;
    last_key_ms = sys_timer_get_ms();
}

void e2_power_on_disconnected(void)
{
    connected    = 0;
    unconnected_since_ms = sys_timer_get_ms();      /* advertising restarts: its own event follows */
}

void e2_power_on_key(void)
{
    last_key_ms = sys_timer_get_ms();
}
