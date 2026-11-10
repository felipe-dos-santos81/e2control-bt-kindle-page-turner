/* Power policy of the Kindle Click app: when the unit switches itself off. SDK-bound; the decision itself
 * is kc_power_should_off (kc_power.c, host-tested). See fw/README.md "Power".
 * Every function runs in the app task (the only writer of the state); the poll timer is added from it. */
#ifndef E2_POWER_H
#define E2_POWER_H

/* App start: set up the USB-power sense pin and start the 1 s power poll. power_off is called once, in the
 * app task, when the unit must soft-power-off (the app's own set_soft_poweroff). */
void e2_power_init(void (*power_off)(void));

void e2_power_on_adv_start(void);       /* BLE_ST_ADV: the 180 s count starts */
void e2_power_on_connected(void);       /* BLE_ST_CONNECT: the 30 min count starts */
void e2_power_on_disconnected(void);    /* BLE_ST_DISCONN: back to the 180 s rule */
void e2_power_on_key(void);             /* any key event: re-arms the 30 min count */

#endif
