/* Report path of the Kindle Click app: the press queue (kc_link) and the 20 ms send tick.
 * SDK-bound; every function runs in the app task. */
#ifndef APP_KINDLE_SEND_H
#define APP_KINDLE_SEND_H

#include "typedef.h"
#include "kc_proto.h"

void kc_send_reset(void);                       /* boot, clear-bond, advertising gave up: queue emptied */
void kc_send_press(kc_action_t a, u8 times);    /* queue a page/sleep press (full queue: dropped) */
void kc_send_link_down(void);                   /* connect or disconnect: not ready, queue kept */
void kc_send_encrypted(void);
void kc_send_cccd_written(void);

#endif
