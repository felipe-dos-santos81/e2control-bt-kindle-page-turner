#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "kc_proto.h"
#include "kc_keys.h"

static void test_adv(void) {
    static const uint8_t want[] = { 0x02,0x01,0x06,  0x05,0xFF,0x71,0x01,0x04,0x43,  0x03,0x03,0x12,0x18 };
    assert(kc_adv_data_len == sizeof want && !memcmp(kc_adv_data, want, sizeof want));
    for (int i = 0; i + 1 < kc_adv_data_len; i++)                 /* never 0xFE03 */
        assert(!(kc_adv_data[i] == 0x03 && kc_adv_data[i + 1] == 0xFE));
}
static void test_pnp(void) {
    static const uint8_t want[7] = { 0x01, 0x71,0x01, 0x43,0x04, 0x00,0x01 };
    assert(!memcmp(kc_pnp_id, want, 7));
}
static void test_reports(void) {
    uint8_t r[KC_REPORT_LEN];
    kc_report_press(KC_NEXT_PAGE, r);  assert(r[2] == 0x4E && r[8] == 0 && r[9] == 0);
    kc_report_press(KC_PREV_PAGE, r);  assert(r[2] == 0x4B && r[3] == 0);
    kc_report_press(KC_SLEEP, r);      assert(r[2] == 0 && r[8] == 0x30 && r[9] == 0x00);
    kc_report_press(KC_CLEAR_BOND, r); for (int i = 0; i < KC_REPORT_LEN; i++) assert(r[i] == 0);
    kc_report_release(r);              for (int i = 0; i < KC_REPORT_LEN; i++) assert(r[i] == 0);
}
static void test_report_map(void) {   /* one application collection, report ID 1, 10 bytes */
    int collections = 0;
    for (int i = 0; i + 1 < kc_report_map_len; i++)
        if (kc_report_map[i] == 0xA1 && kc_report_map[i + 1] == 0x01) collections++;
    assert(collections == 1);
    assert(kc_report_map[0] == 0x05 && kc_report_map[1] == 0x01 && kc_report_map[2] == 0x09 && kc_report_map[3] == 0x06);
}
static void test_keys(void) {
    assert(kc_key_action(1, 0, 0) == KC_PREV_PAGE);
    assert(kc_key_action(2, 0, 0) == KC_NEXT_PAGE);
    assert(kc_key_action(3, 1, 0) == KC_SLEEP);
    assert(kc_key_action(3, 0, 0) == KC_NONE);                    /* click on key 3: nothing */
    assert(kc_key_action(3, 3, 0) == KC_NONE);                    /* release after long press: no click */
    assert(kc_key_action(3, 2, 5) == KC_NONE);                    /* hold repeats: no second Sleep */
    assert(kc_key_action(3, 2, KC_CLEAR_BOND_HOLDS) == KC_CLEAR_BOND);
    assert(kc_key_action(3, 2, KC_CLEAR_BOND_HOLDS + 1) == KC_NONE);
    assert(kc_key_action(1, 1, 0) == KC_NONE && kc_key_action(1, 4, 0) == KC_NONE && kc_key_action(9, 0, 0) == KC_NONE);
}

int main(void) {
    test_adv();        puts("ok test_adv");
    test_pnp();        puts("ok test_pnp");
    test_reports();    puts("ok test_reports");
    test_report_map(); puts("ok test_report_map");
    test_keys();       puts("ok test_keys");
    return 0;
}
