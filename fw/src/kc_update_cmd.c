#include "kc_update_cmd.h"

static const char cmd[] = "JUMPBOOT";
#define CMD_LEN (sizeof cmd - 1)

bool kc_update_cmd_feed(kc_update_cmd_t *st, uint8_t byte)
{
    if (byte == (uint8_t)cmd[st->pos]) {
        if (++st->pos == CMD_LEN) {
            st->pos = 0;
            return true;
        }
        return false;
    }
    /* 'J' occurs only at the start of "JUMPBOOT", so after a mismatch the only possible new start
     * is this byte itself. */
    st->pos = (byte == 'J');
    return false;
}
