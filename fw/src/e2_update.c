/* Entry into the boot area's USB updater, kept as the stock E2 firmware has it.
 * Evidence and open points: fw/README.md "USB update entry". */
#include "system/includes.h"
#include "app_config.h"
#include "asm/gpio.h"
#include "asm/wdt.h"
#include "usb/device/usb_stack.h"
#include "usb/device/cdc.h"
#include "kc_update_cmd.h"
#include "e2_update.h"
#if TCFG_IOKEY_ENABLE
#include "iokey.h"
#endif

#define E2_UBOOT_TAG_ADDR   ((u8 *)0x800)   /* 8-byte RAM tag slot the mask ROM checks after a reset (uboot: inferred) */
#define E2_UPDATE_HOLD_MS   3000            /* keys 1 + 2 held this long right at boot */

extern void hw_mmu_disable(void);                           /* system.a(memory.c) */
extern int  pc_device_event_handler(struct sys_event *e);   /* apps/common/device/usb/device/task_pc.c */

void e2_update_enter(void)
{
    /* Stock FUN_01e02562: memcpy(0x800, .data 0x84a = "ubootZst", 8); FUN_01e14822 (= hw_mmu_disable,
     * JL_MMU->CON &= ~BIT(0)); goto FUN_00004462 (= p33_soft_reset, [0x1a18a0] |= 0x30). */
    static const u8 tag[8] = { 'u', 'b', 'o', 'o', 't', 'Z', 's', 't' };

    memcpy(E2_UBOOT_TAG_ADDR, tag, sizeof tag);
    hw_mmu_disable();
    cpu_reset();        /* p33_soft_reset() */
    while (1);
}

/*------------------------------------------------------------------ USB: {"o":"JUMPBOOT"} */
static kc_update_cmd_t cmd_state;   /* only touched in the USB interrupt */

/* CDC bulk-OUT callback, in the USB interrupt (as the SDK's usb_cdc_wakeup and the stock handler). */
static void cdc_rx(struct usb_device_t *usb_device)
{
    u8 buf[MAXP_SIZE_CDC_BULKOUT];
    u32 len = cdc_read_data(usb_device2id(usb_device), buf, sizeof buf);

    for (u32 i = 0; i < len; i++) {
        if (kc_update_cmd_feed(&cmd_state, buf[i])) {
            e2_update_enter();
        }
    }
}

void e2_update_device_event(struct sys_event *event)
{
    /* The SDK handler starts the CDC device when the OTG detector reports a host ("s:N" in) and stops it
     * on "out" (USB_PC_NO_APP_MODE). It returns 1 after usb_start(), which installed the SDK's echo demo
     * as the receive callback: replace that with ours. */
    if (pc_device_event_handler(event) == 1) {
        cdc_set_wakeup_handler(cdc_rx);
    }
}

/*------------------------------------------------------------------ keys 1 + 2 at power-on */
#if TCFG_IOKEY_ENABLE
extern const struct iokey_platform_data iokey_data;     /* board file */

/* Pressed state of the one-pin IO key with this SDK key number, read as io_get_key_value() does.
 * 0 if no such key is configured. */
static int iokey_pressed(u8 key_value)
{
    for (int i = 0; iokey_data.enable && i < iokey_data.num; i++) {
        const struct iokey_port *k = &iokey_data.port[i];
        if ((k->key_value & ~BIT(7)) != key_value) {
            continue;
        }
        if (k->connect_way == ONE_PORT_TO_LOW) {
            return gpio_read(k->key_type.one_io.port) == 0;
        }
        if (k->connect_way == ONE_PORT_TO_HIGH) {
            return gpio_read(k->key_type.one_io.port) == 1;
        }
    }
    return 0;
}
#endif

void e2_update_boot_keys(void)
{
#if TCFG_IOKEY_ENABLE
    /* ponytail: polls the two pins directly (as app_mouse_dual.c does for its two-key gesture), because
     * the key driver reports one key at a time. AD-ladder keys cannot be read separately: no entry then. */
    for (u32 ms = 0; iokey_pressed(1) && iokey_pressed(2); ms += 10) {
        if (ms >= E2_UPDATE_HOLD_MS) {
            e2_update_enter();
        }
        wdt_clear();
        os_time_dly(1);     /* one 10 ms tick */
    }
#endif
}
