/* Entry into the boot area's USB updater (uboot.boot), as the stock E2 firmware does it.
 * SDK-bound; see fw/README.md "USB update entry". */
#ifndef E2_UPDATE_H
#define E2_UPDATE_H

/* Write the 8-byte tag "ubootZst" at RAM 0x800, disable the MMU, soft-reset the chip.
 * Same three steps as the stock firmware (FUN_01e02562). Does not return. */
void e2_update_enter(void);

/* Call first thing at app start, before BLE starts: if the two board keys TCFG_E2_UPDATE_KEY1/2_PORT
 * are both held for E2_UPDATE_HOLD_MS without a break, enter update mode. Returns at once otherwise.
 * Empty when the board config does not provide the two pins (the build log says which). */
void e2_update_boot_keys(void);

/* Start the USB CDC device and hook its receive path to the update command. Call where stock calls
 * usb_start: after btstack_init() and sys_key_event_enable(). */
void e2_update_usb_start(void);

#endif
