#include "kc_power.h"

bool kc_power_should_off(bool usb_power, bool connected, uint32_t ms_since_adv_start, uint32_t ms_since_last_key)
{
    if (usb_power) {
        return false;
    }
    return connected ? ms_since_last_key >= KC_POWER_IDLE_OFF_MS : ms_since_adv_start >= KC_POWER_ADV_OFF_MS;
}

bool kc_power_restart_battery_monitor(bool monitor_stopped, bool usb_power)
{
    return monitor_stopped && !usb_power;
}
