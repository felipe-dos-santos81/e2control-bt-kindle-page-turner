/* Power-off decision of the Kindle Click app. Pure C, no SDK includes (the timers and pins are in e2_power.c). */
#ifndef KC_POWER_H
#define KC_POWER_H

#include <stdint.h>
#include <stdbool.h>

#define KC_POWER_ADV_OFF_MS   180000u     /* advertising, no connection: power off after 3 min */
#define KC_POWER_IDLE_OFF_MS  1800000u    /* connected, no key event: power off after 30 min */

/* True when the unit must soft-power-off now. USB power present: never. Not connected: only the time since
 * advertising started counts. Connected: only the time since the last key event counts. */
bool kc_power_should_off(bool usb_power, bool connected, uint32_t ms_since_adv_start, uint32_t ms_since_last_key);

/* True when the SDK battery monitor, stopped because a low-battery power-off was skipped on USB power, must be
 * restarted now: it was stopped and USB power is gone. While USB power stays, the monitor stays stopped. */
bool kc_power_restart_battery_monitor(bool monitor_stopped, bool usb_power);

#endif
