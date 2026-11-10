/* Recogniser for the update-entry command on the USB serial byte stream. Pure C, no SDK includes.
 *
 * The command is the JSON object {"o":"JUMPBOOT"}: the 16 bytes the vendor updater writes
 * (JSON.stringify of {o: "JUMPBOOT"}). JSON whitespace (space, tab, CR, LF) is accepted between
 * tokens, not inside the strings. The bytes may arrive in any number of packets and may follow any
 * other bytes, including other JSON objects. Other keys or values are not the command. */
#ifndef KC_UPDATE_CMD_H
#define KC_UPDATE_CMD_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t pos;    /* private: bytes of the command matched so far. Zero-initialise before the first feed. */
} kc_update_cmd_t;

/* Feed one received byte. True exactly when this byte completes the command; the matcher then
 * starts over, so a second command is recognised again. */
bool kc_update_cmd_feed(kc_update_cmd_t *st, uint8_t byte);

#endif
