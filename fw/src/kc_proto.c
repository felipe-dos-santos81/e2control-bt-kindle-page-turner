#include <string.h>
#include "kc_proto.h"

/* Flags 0x06; manufacturer data 71 01 04 43 (Amazon 0x0171, PID 0x0443); UUID 0x1812.
 * Never 0xFE03. */
const uint8_t kc_adv_data[] = {
    0x02, 0x01, 0x06,
    0x05, 0xFF, 0x71, 0x01, 0x04, 0x43,
    0x03, 0x03, 0x12, 0x18,
};
const uint8_t kc_adv_data_len = sizeof kc_adv_data;

/* Report map from the verified protocol document (patricksuo/zectrix-note4-esp32-
 * kindle-page-turner, docs/PROTOCOL.md section 2). It is byte-identical to the
 * vendor's map in 2387LJ app.bin at 0x1E1DC3C, so there is no difference
 * to note. One Application Collection (Generic Desktop / Keyboard), Report ID 1,
 * 8 modifier bits, 1 reserved byte, 6 key bytes, one 16-bit Consumer usage:
 * 10 bytes after the report ID. */
const uint8_t kc_report_map[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
    0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x75, 0x08, 0x95, 0x01, 0x81, 0x01,
    0x19, 0x00, 0x29, 0x65, 0x15, 0x00, 0x25, 0x65,
    0x75, 0x08, 0x95, 0x06, 0x81, 0x00,
    0x05, 0x0C,
    0x19, 0x00, 0x2A, 0xFF, 0x03, 0x15, 0x00, 0x26, 0xFF, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0,
};
const uint16_t kc_report_map_len = sizeof kc_report_map;

/* Bluetooth SIG source, VID 0x0171, PID 0x0443, version 0x0100. */
const uint8_t kc_pnp_id[7] = { 0x01, 0x71, 0x01, 0x43, 0x04, 0x00, 0x01 };

void kc_report_release(uint8_t out[KC_REPORT_LEN])
{
    memset(out, 0, KC_REPORT_LEN);
}

void kc_report_press(kc_action_t a, uint8_t out[KC_REPORT_LEN])
{
    kc_report_release(out);
    switch (a) {
    case KC_NEXT_PAGE: out[2] = 0x4E; break;   /* keyboard Page Down */
    case KC_PREV_PAGE: out[2] = 0x4B; break;   /* keyboard Page Up */
    case KC_SLEEP:     out[8] = 0x30; break;   /* consumer Power, 0x0030 */
    case KC_BRIGHT_UP:   out[8] = 0x6F; break; /* consumer Display Brightness Increment, 0x006F */
    case KC_BRIGHT_DOWN: out[8] = 0x70; break; /* consumer Display Brightness Decrement, 0x0070 */
    default: break;
    }
}
