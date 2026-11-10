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

#endif
