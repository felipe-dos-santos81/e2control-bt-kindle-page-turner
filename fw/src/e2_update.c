/* Entry into the boot area's USB updater, kept as the stock E2 firmware has it.
 * Evidence and open points: fw/README.md "USB update entry". */
#include "system/includes.h"
#include "app_config.h"
#include "asm/gpio.h"
#include "asm/wdt.h"
#include "usb/device/usb_stack.h"
#include "usb/device/cdc.h"
#include "asm/adc_api.h"
#include "app_power_manage.h"
#include "kc_update_cmd.h"
#include "e2_update.h"

#define E2_UBOOT_TAG_ADDR   ((u8 *)0x800)   /* 8-byte RAM tag slot the mask ROM checks after a reset (uboot: inferred) */
#define E2_UPDATE_HOLD_MS   3000            /* keys held this long right at boot */

/* Key entry: the update keys are the two wake-up keys (board config: TCFG_E2_UPDATE_KEYn_PORT is
 * TCFG_E2_WAKE_KEYn_PORT). It is built when both are usable, i.e. configured and not the debug UART TX pin;
 * the board config holds that rule once, as E2_WAKE_KEYn_ENABLE. fw/build.sh reports from sdk.lst whether
 * e2_key_entry_held was linked (the SDK compiles with -w, so a #pragma message would not show). */
#if E2_WAKE_KEY1_ENABLE && E2_WAKE_KEY2_ENABLE
#define E2_KEY_ENTRY 1
#else
#define E2_KEY_ENTRY 0
#endif

extern void hw_mmu_disable(void);                           /* system.a(memory.c) */

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

/*------------------------------------------------------------------ USB: JUMPBOOT */
static kc_update_cmd_t cmd_state;   /* only touched in the USB interrupt */
static volatile u8 status_asked;    /* set in the USB interrupt, cleared in the app task */

/* CDC bulk-OUT callback, in the USB interrupt (as the SDK's usb_cdc_wakeup and the stock handler). */
static void cdc_rx(struct usb_device_t *usb_device)
{
    u8 buf[MAXP_SIZE_CDC_BULKOUT];
    u32 len = cdc_read_data(usb_device2id(usb_device), buf, sizeof buf);

    for (u32 i = 0; i < len; i++) {
        if (kc_update_cmd_feed(&cmd_state, buf[i])) {
            e2_update_enter();
        }
        if (buf[i] == '?') {
            status_asked = 1;
        }
    }
}

/* Board status on the serial port: send '?', get one line. For checking the board facts that are read from the stock
 * firmware and not measured: the chip's own VBAT reading (10 mV units), the battery divider on PA8 and the selector
 * on PA1 (mV at the pin), USB-power sense PA7, charge status PB6. Runs in the app task (the timer is added from it). */
static void status_poll(void *priv)
{
    char line[96];

    if (!status_asked) {
        return;
    }
    status_asked = 0;
    snprintf(line, sizeof line, "vbat=%d pa8=%d pa1=%d pa7=%d pb6=%d up=%d\n", get_vbat_level(), adc_get_voltage(AD_CH_PA8),
            adc_get_voltage(AD_CH_PA1), gpio_read(IO_PORTA_07), gpio_read(IO_PORTB_06), sys_timer_get_ms() / 1000);
    cdc_write_data(0, (u8 *)line, strlen(line));
}

static void analog_in(u8 pin, u32 ch)   /* as the stock board init does for PA8 and PA1 */
{
    gpio_set_direction(pin, 1);
    gpio_set_pull_up(pin, 0);
    gpio_set_pull_down(pin, 0);
    gpio_set_die(pin, 0);
    adc_add_sample_ch(ch);
}

void e2_update_usb_start(void)
{
    usb_start();                        /* stock 0x1e05b8e; SDK task_pc.c: CDC on USB0, echo demo callback */
    cdc_set_wakeup_handler(cdc_rx);     /* stock does this inside its usb_start (handler 0x1e02fc2) */

    analog_in(IO_PORTA_08, AD_CH_PA8);
    analog_in(IO_PORTA_01, AD_CH_PA1);
    gpio_set_direction(IO_PORTB_06, 1); /* charge status: input with pull-up, as stock */
    gpio_set_pull_down(IO_PORTB_06, 0);
    gpio_set_pull_up(IO_PORTB_06, 1);
    gpio_set_die(IO_PORTB_06, 1);
    sys_timer_add(NULL, status_poll, 200);
}

/*------------------------------------------------------------------ keys at power-on */
#if E2_KEY_ENTRY
/* Key to GND, internal pull-up: the stock E2's IO keys (ONE_PORT_TO_LOW). */
static void key_pin_input(u32 pin)
{
    gpio_set_direction(pin, 1);
    gpio_set_pull_down(pin, 0);
    gpio_set_pull_up(pin, 1);
    gpio_set_die(pin, 1);
}

__attribute__((noinline)) static int e2_key_entry_held(void)
{
    return gpio_read(TCFG_E2_UPDATE_KEY1_PORT) == 0 && gpio_read(TCFG_E2_UPDATE_KEY2_PORT) == 0;
}
#endif

void e2_update_boot_keys(void)
{
#if E2_KEY_ENTRY
    /* ponytail: polls the two pins directly (as app_mouse_dual.c does for its two-key gesture): the key
     * driver reports one key at a time. */
    key_pin_input(TCFG_E2_UPDATE_KEY1_PORT);
    key_pin_input(TCFG_E2_UPDATE_KEY2_PORT);
    os_time_dly(1);         /* let the pull-ups settle: one 10 ms tick */
    for (u32 ms = 0; e2_key_entry_held(); ms += 10) {
        if (ms >= E2_UPDATE_HOLD_MS) {
            e2_update_enter();
        }
        wdt_clear();
        os_time_dly(1);
    }
#endif
}
