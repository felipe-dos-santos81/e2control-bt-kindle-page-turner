/* Entry into the boot area's USB updater (uboot.boot), as the stock E2 firmware does it.
 * SDK-bound; see fw/README.md "USB update entry". */
#ifndef E2_UPDATE_H
#define E2_UPDATE_H

#include "system/event.h"

/* Write the 8-byte tag "ubootZst" at RAM 0x800, disable the MMU, soft-reset the chip.
 * Same three steps as the stock firmware (FUN_01e02562). Does not return. */
void e2_update_enter(void);

/* Call first thing at app start, before BLE starts: if SDK keys 1 and 2 are both held for
 * E2_UPDATE_HOLD_MS without a break, enter update mode. Returns at once otherwise. */
void e2_update_boot_keys(void);

/* Pass every SYS_DEVICE_EVENT: starts/stops the USB CDC device when the SDK's OTG detector
 * reports a host, and hooks the CDC receive path to the update-entry command. */
void e2_update_device_event(struct sys_event *event);

#endif
