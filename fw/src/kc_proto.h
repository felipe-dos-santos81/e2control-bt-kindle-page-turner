/* Kindle Click protocol tables. Pure C, no SDK includes. */
#ifndef KC_PROTO_H
#define KC_PROTO_H

#include <stdint.h>

#define KC_REPORT_ID   1
#define KC_REPORT_LEN  10   /* without the report ID: mod, rsvd, key[6], consumer lo, hi */

typedef enum { KC_NONE = 0, KC_PREV_PAGE, KC_NEXT_PAGE, KC_SLEEP, KC_CLEAR_BOND } kc_action_t;

extern const uint8_t  kc_adv_data[];     extern const uint8_t  kc_adv_data_len;
extern const uint8_t  kc_report_map[];   extern const uint16_t kc_report_map_len;
extern const uint8_t  kc_pnp_id[7];

/* Fills out with the key-down report for a. All zero for actions with no report. */
void kc_report_press(kc_action_t a, uint8_t out[KC_REPORT_LEN]);
/* Fills out with the all-zero release report. */
void kc_report_release(uint8_t out[KC_REPORT_LEN]);

#endif
