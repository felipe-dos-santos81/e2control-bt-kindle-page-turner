/* Recogniser for the update-entry command on the USB serial byte stream. Pure C, no SDK includes.
 *
 * The vendor updater writes {"o":"JUMPBOOT"} (JSON.stringify, 16 bytes). As the stock firmware
 * (strstr(packet, "JUMPBOOT")), only the 8 ASCII bytes JUMPBOOT are looked for, received
 * contiguously anywhere in the stream; unlike stock, they may also be split across packets. */
#ifndef KC_UPDATE_CMD_H
#define KC_UPDATE_CMD_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t pos;    /* private: bytes of JUMPBOOT matched so far. Zero-initialise before the first feed. */
} kc_update_cmd_t;

/* Feed one received byte. True exactly when this byte completes JUMPBOOT; the matcher then starts
 * over, so a second command is recognised again. */
bool kc_update_cmd_feed(kc_update_cmd_t *st, uint8_t byte);

#endif
