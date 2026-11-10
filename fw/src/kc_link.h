/* Link-ready gate and press queue. Pure C, no SDK includes.
 * Time is a wrapping uint32_t millisecond counter, compared by subtraction. Callers pass a freshly
 * sampled, monotonic millisecond clock on every call that takes now_ms. A CCCD write is only
 * meaningful after encryption on the same connection (kc_link_on_disconnect forgets it). */
#ifndef KC_LINK_H
#define KC_LINK_H

#include <stdint.h>
#include <stdbool.h>
#include "kc_proto.h"

#define KC_READY_AFTER_CCCD_MS  800
#define KC_READY_AFTER_ENC_MS   5000
#define KC_QUEUE_LEN            4

typedef struct {
    /* private: use the functions below */
    bool        encrypted;
    bool        cccd_seen;
    uint32_t    enc_at;
    uint32_t    cccd_at;
    uint8_t     head, count;
    kc_action_t q[KC_QUEUE_LEN];
} kc_link_t;

/* At boot and on clear-bond: not ready, queue emptied. */
void        kc_link_reset(kc_link_t *l);
/* Not ready; queued presses are kept. */
void        kc_link_on_disconnect(kc_link_t *l);
void        kc_link_on_encrypted(kc_link_t *l, uint32_t now_ms);
void        kc_link_on_cccd_write(kc_link_t *l, uint32_t now_ms);
/* Encrypted and (800 ms since the last CCCD write, or 5 s since encryption). */
bool        kc_link_ready(const kc_link_t *l, uint32_t now_ms);
/* False when full; the new press is dropped and earlier ones kept. */
bool        kc_link_push(kc_link_t *l, kc_action_t a);
/* KC_NONE unless ready and non-empty. */
kc_action_t kc_link_pop(kc_link_t *l, uint32_t now_ms);
/* Queued presses. */
uint8_t     kc_link_count(const kc_link_t *l);
/* Encryption is up on the current connection. */
bool        kc_link_is_encrypted(const kc_link_t *l);

#endif
