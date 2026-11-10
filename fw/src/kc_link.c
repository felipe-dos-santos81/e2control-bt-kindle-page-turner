#include <string.h>
#include "kc_link.h"

void kc_link_reset(kc_link_t *l)
{
    memset(l, 0, sizeof *l);
}

void kc_link_on_disconnect(kc_link_t *l)
{
    l->encrypted = false;
    l->cccd_seen = false;
}

void kc_link_on_encrypted(kc_link_t *l, uint32_t now_ms)
{
    l->encrypted = true;
    l->enc_at = now_ms;
}

void kc_link_on_cccd_write(kc_link_t *l, uint32_t now_ms)
{
    l->cccd_seen = true;
    l->cccd_at = now_ms;
}

bool kc_link_ready(const kc_link_t *l, uint32_t now_ms)
{
    if (!l->encrypted) return false;
    if (l->cccd_seen && (uint32_t)(now_ms - l->cccd_at) >= KC_READY_AFTER_CCCD_MS) return true;
    return (uint32_t)(now_ms - l->enc_at) >= KC_READY_AFTER_ENC_MS;
}

bool kc_link_push(kc_link_t *l, kc_action_t a)
{
    if (l->count == KC_QUEUE_LEN) return false;
    l->q[(l->head + l->count) % KC_QUEUE_LEN] = a;
    l->count++;
    return true;
}

kc_action_t kc_link_pop(kc_link_t *l, uint32_t now_ms)
{
    if (l->count == 0 || !kc_link_ready(l, now_ms)) return KC_NONE;
    kc_action_t a = l->q[l->head];
    l->head = (l->head + 1) % KC_QUEUE_LEN;
    l->count--;
    return a;
}

uint8_t kc_link_count(const kc_link_t *l)
{
    return l->count;
}

bool kc_link_is_encrypted(const kc_link_t *l)
{
    return l->encrypted;
}
