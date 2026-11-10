#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "kc_proto.h"
#include "kc_keys.h"
#include "kc_link.h"
#include "kc_update_cmd.h"

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
    /* multi-click: count from kc_key_repeat, action from the CLICK event */
    assert(kc_key_repeat(1, 0) == 1 && kc_key_repeat(2, 0) == 1);
    assert(kc_key_repeat(2, 4) == 2 && kc_key_repeat(2, 5) == 3 && kc_key_repeat(1, 6) == 4);
    assert(kc_key_repeat(1, 7) == KC_QUEUE_LEN);                  /* five clicks capped at the queue */
    assert(kc_key_repeat(1, 1) == 0 && kc_key_repeat(1, 2) == 0 && kc_key_repeat(1, 3) == 0 && kc_key_repeat(1, 8) == 0);
    assert(kc_key_repeat(3, 0) == 0 && kc_key_repeat(3, 4) == 0 && kc_key_repeat(9, 4) == 0);   /* click has no action */
}
static void test_link(void) {
    kc_link_t l; kc_link_reset(&l);
    assert(kc_link_push(&l, KC_NEXT_PAGE));
    assert(kc_link_pop(&l, 0) == KC_NONE);                        /* not connected */
    kc_link_on_encrypted(&l, 1000);
    assert(!kc_link_ready(&l, 5999) && kc_link_ready(&l, 6000));  /* 5 s fallback */
    kc_link_on_cccd_write(&l, 1500);
    kc_link_on_cccd_write(&l, 1700);                              /* last CCCD write counts */
    assert(!kc_link_ready(&l, 2499) && kc_link_ready(&l, 2500));
    assert(kc_link_pop(&l, 2500) == KC_NEXT_PAGE);                /* held press sent once */
    assert(kc_link_pop(&l, 2501) == KC_NONE);
    for (int i = 0; i < KC_QUEUE_LEN; i++) assert(kc_link_push(&l, KC_PREV_PAGE));
    assert(!kc_link_push(&l, KC_NEXT_PAGE));                      /* bounded */
    kc_link_on_disconnect(&l);
    assert(!kc_link_ready(&l, 99999) && kc_link_pop(&l, 99999) == KC_NONE);
    kc_link_on_encrypted(&l, 0xFFFFFF00u);                        /* timer wrap */
    kc_link_on_cccd_write(&l, 0xFFFFFF00u);
    assert(kc_link_ready(&l, 0xFFFFFF00u + 800u));
    assert(kc_link_pop(&l, 0xFFFFFF00u + 800u) == KC_PREV_PAGE);  /* kept across the disconnect */
}

/* Feeds s; returns how many bytes completed a command, and the index of the last one in *at. */
static int cmd_feed(kc_update_cmd_t *st, const char *s, int *at) {
    int hits = 0;
    for (int i = 0; s[i]; i++)
        if (kc_update_cmd_feed(st, (uint8_t)s[i])) { hits++; if (at) *at = i; }
    return hits;
}
static void test_update_cmd(void) {
    static const char cmd[] = "{\"o\":\"JUMPBOOT\"}";     /* what the updater writes: JSON.stringify */
    kc_update_cmd_t st = {0};
    int at = -1;
    assert(cmd_feed(&st, cmd, &at) == 1 && at == (int)sizeof cmd - 2);  /* true only on the last byte */
    assert(cmd_feed(&st, cmd, &at) == 1);                         /* starts over: second command too */
    /* JSON whitespace between tokens */
    st = (kc_update_cmd_t){0};
    assert(cmd_feed(&st, " {\r\n\t\"o\" :  \"JUMPBOOT\"\n}", NULL) == 1);
    /* split across packets: state carries over */
    st = (kc_update_cmd_t){0};
    assert(cmd_feed(&st, "{\"o\":\"JUM", NULL) == 0 && cmd_feed(&st, "PBO", NULL) == 0 && cmd_feed(&st, "OT\"}", NULL) == 1);
    st = (kc_update_cmd_t){0};
    for (int i = 0; cmd[i]; i++) { char one[2] = { cmd[i], 0 }; assert(cmd_feed(&st, one, NULL) == (cmd[i + 1] == 0)); }
    /* after other bytes and other JSON objects, including the updater's get queries */
    st = (kc_update_cmd_t){0};
    assert(cmd_feed(&st, "\x01\xAA garbage }}{{", NULL) == 0);
    assert(cmd_feed(&st, "{\"o\":\"get\",\"t\":\"battery\"}{\"o\":\"get\",\"t\":\"mac\"}{\"o\":\"JUMPBOOT\"}", NULL) == 1);
    st = (kc_update_cmd_t){0};
    assert(cmd_feed(&st, "{\"o\":\"JUMP{\"o\":\"JUMPBOOT\"}", NULL) == 1);   /* broken one, then a whole one */
    st = (kc_update_cmd_t){0};
    assert(cmd_feed(&st, "{\"o\":\"JUMPBOOT\"{\"o\":\"JUMPBOOT\"}", NULL) == 1);
    /* not the command */
    const char *no[] = {
        "JUMPBOOT", "\"JUMPBOOT\"", "{\"o\":\"JUMPBOOTX\"}", "{\"o\":\"jumpboot\"}", "{\"o\":\"JUMP BOOT\"}",
        "{\"o\":\" JUMPBOOT\"}", "{\" o\":\"JUMPBOOT\"}", "{\"O\":\"JUMPBOOT\"}", "{\"t\":\"JUMPBOOT\"}",
        "{\"o\":\"get\",\"t\":\"JUMPBOOT\"}", "{\"o\":\"JUMPBOOT\",\"t\":1}", "{\"o\":JUMPBOOT}", "{'o':'JUMPBOOT'}",
        "{\"o\"\"JUMPBOOT\"}", "{\"o\":\"JUMPBOOT\"]", "{\"o\":\"JUMPBOO\"}", "{\"o\":\"get\",\"t\":\"firmware\"}",
    };
    for (unsigned i = 0; i < sizeof no / sizeof no[0]; i++) { st = (kc_update_cmd_t){0}; assert(cmd_feed(&st, no[i], NULL) == 0); }
    /* every byte value, twice over, never matches by itself */
    st = (kc_update_cmd_t){0};
    for (int n = 0; n < 512; n++) assert(!kc_update_cmd_feed(&st, (uint8_t)n));
    assert(cmd_feed(&st, cmd, NULL) == 1);                        /* and does not leave the matcher stuck */
}

int main(void) {
    test_adv();        puts("ok test_adv");
    test_pnp();        puts("ok test_pnp");
    test_reports();    puts("ok test_reports");
    test_report_map(); puts("ok test_report_map");
    test_keys();       puts("ok test_keys");
    test_link();       puts("ok test_link");
    test_update_cmd(); puts("ok test_update_cmd");
    return 0;
}
